#include "am_model.h"

#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *field(const cJSON *object, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

static bool text(const cJSON *object, const char *key, char *out, size_t size)
{
    const cJSON *item = field(object, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    am_utf8_copy(out, size, item->valuestring);
    return true;
}

static bool number(const cJSON *object, const char *key, double *out)
{
    const cJSON *item = field(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *out = item->valuedouble;
    return true;
}

static int64_t nullable_integer(const cJSON *object, const char *key)
{
    double value;
    if (!number(object, key, &value) || value < 0 || value > 9007199254740991.0 || floor(value) != value) return -1;
    return (int64_t)value;
}

static bool count(const cJSON *object, const char *key, int *out)
{
    int64_t value = nullable_integer(object, key);
    if (value < 0 || value > 2147483647) return false;
    *out = (int)value;
    return true;
}

static bool minute(const cJSON *object, const char *key, unsigned *out)
{
    int value;
    if (!count(object, key, &value) || value >= 1440) return false;
    *out = (unsigned)value;
    return true;
}

static am_tokens_t tokens(const cJSON *object)
{
    am_tokens_t value = {
        .input = nullable_integer(object, "input"),
        .output = nullable_integer(object, "output"),
        .cache_read = nullable_integer(object, "cacheRead"),
        .cache_write = nullable_integer(object, "cacheWrite"),
        .reasoning = nullable_integer(object, "reasoning"),
        .total = nullable_integer(object, "total"),
        .input_includes_cache = cJSON_IsTrue(field(object, "inputIncludesCache")),
        .output_includes_reasoning = cJSON_IsTrue(field(object, "outputIncludesReasoning")),
    };
    return value;
}

static bool parse_task(const cJSON *object, am_task_t *task)
{
    if (!text(object, "id", task->id, sizeof(task->id))
        || !text(object, "client", task->client, sizeof(task->client))
        || !text(object, "title", task->title, sizeof(task->title))
        || !text(object, "project", task->project, sizeof(task->project))
        || !text(object, "status", task->status, sizeof(task->status))
        || !count(object, "turnIndex", &task->turn_index)
        || !count(object, "childCount", &task->child_count)) return false;
    task->duration_seconds = nullable_integer(object, "durationSeconds");
    task->observed_at = nullable_integer(object, "observedAt");
    task->tokens = tokens(field(object, "tokens"));
    return true;
}

static bool parse_quota(const cJSON *object, am_quota_t *quota)
{
    if (!text(object, "id", quota->id, sizeof(quota->id))
        || !text(object, "label", quota->label, sizeof(quota->label))
        || !text(object, "provider", quota->provider, sizeof(quota->provider))
        || !text(object, "state", quota->state, sizeof(quota->state))
        || !text(object, "detail", quota->detail, sizeof(quota->detail))) return false;
    int64_t window = nullable_integer(object, "windowMinutes");
    quota->window_minutes = window >= 0 && window <= 2147483647 ? (int)window : -1;
    quota->used_percent = -1;
    double percentage;
    if (number(object, "usedPercent", &percentage) && percentage >= 0 && percentage <= 100) {
        quota->used_percent = percentage;
    }
    quota->resets_at = nullable_integer(object, "resetsAt");
    quota->observed_at = nullable_integer(object, "observedAt");
    return true;
}

static bool parse_alert(const cJSON *object, am_alert_t *alert)
{
    if (!text(object, "id", alert->id, sizeof(alert->id))
        || !text(object, "taskId", alert->task_id, sizeof(alert->task_id))
        || !text(object, "kind", alert->kind, sizeof(alert->kind))
        || !text(object, "title", alert->title, sizeof(alert->title))) return false;
    if (strcmp(alert->kind, "completed") != 0 && strcmp(alert->kind, "failed") != 0
        && strcmp(alert->kind, "needs_action") != 0 && strcmp(alert->kind, "reset_news") != 0) return false;
    alert->created_at = nullable_integer(object, "createdAt");
    alert->unread = cJSON_IsTrue(field(object, "unread"));
    return true;
}

static bool parse_snapshot(const cJSON *root, am_snapshot_t *snapshot)
{
    const cJSON *summary = field(root, "summary");
    const cJSON *settings = field(root, "settings");
    const cJSON *tasks = field(root, "tasks");
    const cJSON *quotas = field(root, "quotas");
    const cJSON *alerts = field(root, "alerts");
    int64_t sequence = nullable_integer(root, "seq");
    snapshot->sent_at = nullable_integer(root, "sentAt");
    if (sequence < 0 || snapshot->sent_at < 0 || !minute(root, "localMinute", &snapshot->local_minute)
        || !count(summary, "running", &snapshot->running)
        || !count(summary, "needsAction", &snapshot->needs_action)
        || !count(summary, "completedToday", &snapshot->completed_today)
        || !cJSON_IsArray(tasks) || !cJSON_IsArray(quotas) || !cJSON_IsArray(alerts)
        || !cJSON_IsBool(field(settings, "muted"))
        || !minute(settings, "quietStart", &snapshot->quiet_start)
        || !minute(settings, "quietEnd", &snapshot->quiet_end)) return false;
    snapshot->sequence = (uint64_t)sequence;
    snapshot->muted = cJSON_IsTrue(field(settings, "muted"));
    const cJSON *news = field(root, "resetNews");
    snapshot->news_published_at = nullable_integer(news, "publishedAt");
    snapshot->news_checked_at = nullable_integer(news, "checkedAt");
    if (cJSON_IsObject(news)) {
        (void)text(news, "title", snapshot->news_title, sizeof(snapshot->news_title));
        (void)text(news, "state", snapshot->news_state, sizeof(snapshot->news_state));
        (void)text(news, "timingText", snapshot->news_timing, sizeof(snapshot->news_timing));
    }
    if (cJSON_GetArraySize(tasks) > AM_TASK_MAX || cJSON_GetArraySize(quotas) > AM_QUOTA_MAX
        || cJSON_GetArraySize(alerts) > AM_ALERT_MAX) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, tasks) {
        if (!parse_task(item, &snapshot->tasks[snapshot->task_count++])) return false;
    }
    cJSON_ArrayForEach(item, quotas) {
        if (!parse_quota(item, &snapshot->quotas[snapshot->quota_count++])) return false;
    }
    cJSON_ArrayForEach(item, alerts) {
        if (!parse_alert(item, &snapshot->alerts[snapshot->alert_count++])) return false;
    }
    return true;
}

static bool seen_event(const am_model_t *model, const char *id)
{
    for (size_t i = 0; i < AM_ALERT_MAX; i++) {
        if (strcmp(model->event_ids[i], id) == 0) return true;
    }
    return false;
}

static void remember_event(am_model_t *model, const char *id)
{
    am_utf8_copy(model->event_ids[model->event_cursor], AM_ID_MAX, id);
    model->event_cursor = (model->event_cursor + 1) % AM_ALERT_MAX;
}

static bool event_was_read(const am_model_t *model, const char *id)
{
    for (size_t i = 0; i < model->pending.ack_count; i++) {
        if (strcmp(model->pending.ack_ids[i], id) == 0) return true;
    }
    for (size_t i = 0; i < model->snapshot.alert_count; i++) {
        if (strcmp(model->snapshot.alerts[i].id, id) == 0 && !model->snapshot.alerts[i].unread) return true;
    }
    return false;
}

bool am_protocol_apply(am_model_t *model, const char *line, size_t length,
                       am_transport_t transport, int64_t now_ms, am_effect_t *effect)
{
    memset(effect, 0, sizeof(*effect));
    if (length == 0 || length > AM_LINE_MAX || line[0] != '{') return false;
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(line, length + 1, &parse_end, true);
    if (!root) return false;
    bool applied = false;
    const cJSON *version = field(root, "v");
    const cJSON *type = field(root, "type");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1
        || !cJSON_IsString(type) || !type->valuestring) goto done;
    if (strcmp(type->valuestring, "acknowledged") == 0) {
        char id[AM_ID_MAX];
        if (text(root, "id", id, sizeof(id))) {
            for (size_t i = 0; i < model->pending.ack_count; i++) {
                if (strcmp(model->pending.ack_ids[i], id) != 0) continue;
                memmove(&model->pending.ack_ids[i], &model->pending.ack_ids[i + 1],
                    (model->pending.ack_count - i - 1) * sizeof(model->pending.ack_ids[0]));
                model->pending.ack_count--;
                effect->pending_changed = true;
                applied = true;
                break;
            }
        }
    } else if (strcmp(type->valuestring, "mute_applied") == 0) {
        const cJSON *muted = field(root, "muted");
        if (cJSON_IsBool(muted) && model->pending.has_mute
            && model->pending.muted == cJSON_IsTrue(muted)) {
            model->pending.has_mute = false;
            effect->pending_changed = true;
            applied = true;
        }
    } else if (strcmp(type->valuestring, "hello") == 0) {
        const cJSON *host = field(root, "host");
        if (cJSON_IsString(host) && strcmp(host->valuestring, "agent-monitor") == 0) {
            effect->hello = true;
            applied = true;
        }
    } else if (strcmp(type->valuestring, "snapshot") == 0) {
        am_snapshot_t *next = calloc(1, sizeof(*next));
        if (!next) goto done;
        if (parse_snapshot(root, next)) {
            bool first = !model->has_snapshot;
            effect->wake = first || !am_model_online(model, now_ms);
            if (model->pending.has_mute) next->muted = model->pending.muted;
            for (size_t i = 0; i < next->alert_count; i++) {
                for (size_t j = 0; j < model->pending.ack_count; j++) {
                    if (strcmp(next->alerts[i].id, model->pending.ack_ids[j]) == 0) next->alerts[i].unread = false;
                }
            }
            effect->settings_changed = first || model->snapshot.muted != next->muted
                || model->snapshot.quiet_start != next->quiet_start || model->snapshot.quiet_end != next->quiet_end;
            am_model_replace_snapshot(model, next);
            model->received_ms = now_ms;
            model->transport = transport;
            model->has_snapshot = true;
            if (first) {
                for (size_t i = 0; i < next->alert_count; i++) remember_event(model, next->alerts[i].id);
            }
            effect->redraw = true;
            effect->received = true;
            applied = true;
        }
        free(next);
    } else if (strcmp(type->valuestring, "event") == 0) {
        am_alert_t alert = {0};
        if (!am_model_online(model, now_ms) || !parse_alert(field(root, "event"), &alert)
            || seen_event(model, alert.id)) goto done;
        remember_event(model, alert.id);
        // A newer snapshot or local acknowledgement wins over an older queued event.
        if (!alert.unread || event_was_read(model, alert.id)) goto done;
        bool found = false;
        for (size_t i = 0; i < model->snapshot.alert_count; i++) {
            if (strcmp(model->snapshot.alerts[i].id, alert.id) == 0) {
                model->snapshot.alerts[i] = alert;
                found = true;
                break;
            }
        }
        if (!found) {
            if (model->snapshot.alert_count < AM_ALERT_MAX) model->snapshot.alert_count++;
            memmove(&model->snapshot.alerts[1], &model->snapshot.alerts[0],
                    (model->snapshot.alert_count - 1) * sizeof(alert));
            model->snapshot.alerts[0] = alert;
        }
        effect->wake = true;
        effect->redraw = true;
        effect->sound = cJSON_IsTrue(field(root, "sound")) && !model->snapshot.muted
            && !am_quiet(am_model_local_minute(model, now_ms), model->snapshot.quiet_start, model->snapshot.quiet_end);
        am_utf8_copy(effect->kind, sizeof(effect->kind), alert.kind);
        am_utf8_copy(effect->alert_id, sizeof(effect->alert_id), alert.id);
        applied = true;
    }
done:
    cJSON_Delete(root);
    return applied;
}
