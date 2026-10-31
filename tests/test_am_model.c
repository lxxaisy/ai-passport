#include "am_model.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static am_model_t model;
static unsigned framed_lines;
static char framed_last[128];

static void capture(const char *line, size_t length, void *context)
{
    (void)context;
    assert(length == strlen(line));
    framed_lines++;
    am_utf8_copy(framed_last, sizeof(framed_last), line);
}

static bool apply(const char *line, int64_t now, am_effect_t *effect)
{
    return am_protocol_apply(&model, line, strlen(line), AM_USB, now, effect);
}

static char *read_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    char *text = calloc(AM_LINE_MAX + 1, 1);
    assert(text);
    size_t size = fread(text, 1, AM_LINE_MAX, file);
    assert(feof(file));
    fclose(file);
    while (size > 0 && (text[size - 1] == '\r' || text[size - 1] == '\n')) text[--size] = '\0';
    return text;
}

static void framing(void)
{
    am_framer_t *framer = calloc(1, sizeof(*framer));
    assert(framer);
    const char *first = "{\"title\":\"中文\"}\n";
    for (size_t i = 0; i < strlen(first); i++) am_framer_feed(framer, first + i, 1, capture, NULL);
    assert(framed_lines == 1 && strcmp(framed_last, "{\"title\":\"中文\"}") == 0);
    char *oversized = malloc(AM_LINE_MAX + 10);
    assert(oversized);
    memset(oversized, 'x', AM_LINE_MAX + 9);
    am_framer_feed(framer, oversized, AM_LINE_MAX + 9, capture, NULL);
    am_framer_feed(framer, "\n{}\r\n", 5, capture, NULL);
    assert(framed_lines == 2 && strcmp(framed_last, "{}") == 0);
    free(oversized);
    free(framer);

    char truncated[8];
    am_utf8_copy(truncated, sizeof(truncated), "中文任务");
    assert(strcmp(truncated, "中文") == 0);
    am_utf8_copy(truncated, 1, "中文");
    assert(truncated[0] == '\0');
}

static void quiet_hours(void)
{
    assert(am_quiet(1320, 1320, 480));
    assert(am_quiet(0, 1320, 480));
    assert(am_quiet(479, 1320, 480));
    assert(!am_quiet(480, 1320, 480));
    assert(!am_quiet(1319, 1320, 480));
    assert(am_quiet(600, 540, 660));
    assert(!am_quiet(600, 600, 600));
}

static void navigation(void)
{
    am_model_init(&model);
    model.snapshot.task_count = 8;
    model.snapshot.alert_count = 2;
    model.snapshot.quota_count = 6;
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_NONE);
    assert(model.page == AM_PAGE_TASKS && model.selected == 0);
    (void)am_model_navigate(&model, AM_NAV_UP);
    assert(model.selected == 7);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_TASK_DETAIL && model.detail_index == 7 && model.detail_page == 0);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.detail_page == 1);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    assert(model.detail_page == 0);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.page == AM_PAGE_TASKS && model.selected == 7);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.page == AM_PAGE_OVERVIEW && model.selected == 0);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_ALERTS && model.selected == 0);
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_ACK);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.page == AM_PAGE_OVERVIEW && model.selected == 1);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_NEWS);
    (void)am_model_navigate(&model, AM_NAV_UP);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_NEWS);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.page == AM_PAGE_OVERVIEW && model.selected == 2);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_QUOTAS && model.detail_page == 0);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.detail_page == 1);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.detail_page == 2);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.detail_page == 0);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    assert(model.selected == 1 && model.detail_page == 0);
    (void)am_model_navigate(&model, AM_NAV_UP);
    assert(model.selected == 0);
    (void)am_model_navigate(&model, AM_NAV_UP);
    assert(model.selected == 5);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.page == AM_PAGE_OVERVIEW && model.selected == 3);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_SETTINGS);
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_MUTE);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_BRIGHTNESS);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_TIMEOUT);
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    assert(am_model_navigate(&model, AM_NAV_OK) == AM_ACTION_NONE);
    assert(model.page == AM_PAGE_OVERVIEW && model.selected == 4);
    (void)am_model_navigate(&model, AM_NAV_BACK);
    assert(model.selected == 4);
    model.page = AM_PAGE_TASKS;
    model.selected = 0;
    model.snapshot.task_count = 0;
    (void)am_model_navigate(&model, AM_NAV_UP);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_TASKS && model.selected == 0);
    model.page = AM_PAGE_QUOTAS;
    model.snapshot.quota_count = 0;
    (void)am_model_navigate(&model, AM_NAV_DOWN);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.selected == 0);
}

static void protocol(const char *fixture)
{
    am_model_init(&model);
    am_effect_t effect;
    assert(!am_model_online(&model, 0));
    assert(apply("{\"v\":1,\"type\":\"hello\",\"host\":\"agent-monitor\"}", 0, &effect));
    assert(effect.hello && !effect.sound);
    assert(apply(fixture, 1000, &effect));
    assert(effect.received && effect.wake && !effect.sound);
    assert(model.snapshot.running == 1 && model.snapshot.needs_action == 0);
    assert(model.snapshot.tasks[0].duration_seconds == 0);
    assert(model.snapshot.tasks[0].observed_at == -1);
    assert(model.snapshot.tasks[0].tokens.input == 0);
    assert(model.snapshot.tasks[0].tokens.output == -1);
    assert(model.snapshot.quotas[0].used_percent == -1);
    assert(model.snapshot.quotas[0].resets_at == -1);
    assert(am_model_online(&model, 21000));
    assert(!am_model_online(&model, 21001));
    assert(strcmp(model.snapshot.tasks[0].status, "running") == 0);

    const char *old_event = "{\"v\":1,\"type\":\"event\",\"sound\":true,\"event\":{\"id\":\"historical-alert\",\"taskId\":\"old\",\"kind\":\"completed\",\"title\":\"历史任务\",\"createdAt\":1792580000,\"unread\":true}}";
    assert(!apply(old_event, 1500, &effect));
    assert(!effect.sound);

    const char *new_event = "{\"v\":1,\"type\":\"event\",\"sound\":true,\"event\":{\"id\":\"new-alert\",\"taskId\":\"new\",\"kind\":\"completed\",\"title\":\"新任务\",\"createdAt\":1792581700,\"unread\":true}}";
    assert(apply(new_event, 1500, &effect));
    assert(effect.wake && effect.sound);
    assert(am_model_unread(&model) == 2);
    assert(!apply(new_event, 1600, &effect));
    char ack[AM_ID_MAX];
    assert(am_model_ack(&model, 0, ack, sizeof(ack)));
    assert(strcmp(ack, "new-alert") == 0);
    assert(!am_model_ack(&model, 0, ack, sizeof(ack)));
    assert(am_model_unread(&model) == 1);
    assert(model.pending.ack_count == 1);
    assert(am_model_ack_pending(&model, "new-alert"));
    assert(!am_model_ack_pending(&model, "other-alert"));
    assert(apply("{\"v\":1,\"type\":\"acknowledged\",\"id\":\"new-alert\"}", 1700, &effect));
    assert(effect.pending_changed && model.pending.ack_count == 0);
    assert(!am_model_ack_pending(&model, "new-alert"));
    assert(!apply("{\"v\":1,\"type\":\"acknowledged\",\"id\":\"new-alert\"}", 1750, &effect));

    am_model_set_mute(&model, true);
    assert(apply(fixture, 1800, &effect));
    assert(model.snapshot.muted && model.pending.has_mute);
    assert(apply("{\"v\":1,\"type\":\"mute_applied\",\"muted\":true}", 1900, &effect));
    assert(effect.pending_changed && !model.pending.has_mute);
    assert(am_model_ack(&model, 0, ack, sizeof(ack)));
    assert(apply(fixture, 1950, &effect));
    assert(model.pending.ack_count == 1 && !model.snapshot.alerts[0].unread);
    assert(apply("{\"v\":1,\"type\":\"acknowledged\",\"id\":\"historical-alert\"}", 1975, &effect));
    assert(model.pending.ack_count == 0);

    cJSON *snapshot = cJSON_Parse(fixture);
    assert(snapshot);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(snapshot, "localMinute"), 1320);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(snapshot, "seq"), 2);
    cJSON *quota = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(snapshot, "quotas"), 0);
    cJSON_ReplaceItemInObjectCaseSensitive(quota, "usedPercent", cJSON_CreateNumber(0));
    cJSON *task = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(snapshot, "tasks"), 0);
    cJSON_ReplaceItemInObjectCaseSensitive(task, "project", cJSON_CreateString(""));
    char *night = cJSON_PrintUnformatted(snapshot);
    assert(apply(night, 2000, &effect));
    assert(!effect.sound && model.snapshot.quotas[0].used_percent == 0);
    assert(strcmp(model.snapshot.tasks[0].project, "") == 0);
    cJSON_free(night);
    cJSON_Delete(snapshot);

    const char *night_event = "{\"v\":1,\"type\":\"event\",\"sound\":true,\"event\":{\"id\":\"night-alert\",\"taskId\":\"night\",\"kind\":\"failed\",\"title\":\"夜间任务\",\"createdAt\":1792581800,\"unread\":true}}";
    assert(apply(night_event, 2500, &effect));
    assert(effect.wake && !effect.sound);
    model.snapshot.muted = true;
    model.snapshot.local_minute = 600;
    const char *muted_event = "{\"v\":1,\"type\":\"event\",\"sound\":true,\"event\":{\"id\":\"muted-alert\",\"taskId\":\"mute\",\"kind\":\"needs_action\",\"title\":\"待处理\",\"createdAt\":1792581900,\"unread\":true}}";
    assert(apply(muted_event, 2600, &effect));
    assert(effect.wake && !effect.sound);
    uint64_t sequence = model.snapshot.sequence;
    assert(!apply("{\"v\":2,\"type\":\"snapshot\"}", 3000, &effect));
    assert(!apply("{\"v\":1,\"type\":\"snapshot\",\"seq\":3}", 3000, &effect));
    assert(model.snapshot.sequence == sequence);
    assert(!apply("ESP log output", 3000, &effect));
    assert(!apply("{", 3000, &effect));
}

static void queued_events_after_snapshot(const char *baseline, const char *read_snapshot, const char *queued_event)
{
    am_model_init(&model);
    am_effect_t effect;
    assert(apply(baseline, 1000, &effect));
    assert(apply(read_snapshot, 1100, &effect));
    assert(am_model_unread(&model) == 0);
    assert(!apply(queued_event, 1200, &effect));
    assert(am_model_unread(&model) == 0 && !effect.sound && !effect.wake);

    cJSON *snapshot = cJSON_Parse(read_snapshot);
    cJSON *event = cJSON_Parse(queued_event);
    assert(snapshot && event);
    cJSON *alert = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(snapshot, "alerts"), 0);
    cJSON *event_alert = cJSON_GetObjectItemCaseSensitive(event, "event");
    cJSON_ReplaceItemInObjectCaseSensitive(alert, "id", cJSON_CreateString("late-unread-event"));
    cJSON_ReplaceItemInObjectCaseSensitive(alert, "unread", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObjectCaseSensitive(event_alert, "id", cJSON_CreateString("late-unread-event"));
    char *line = cJSON_PrintUnformatted(snapshot);
    assert(apply(line, 1300, &effect));
    cJSON_free(line);
    line = cJSON_PrintUnformatted(event);
    assert(apply(line, 1400, &effect));
    assert(effect.sound && effect.wake && am_model_unread(&model) == 1);
    cJSON_free(line);

    cJSON_ReplaceItemInObjectCaseSensitive(alert, "id", cJSON_CreateString("late-pending-event"));
    cJSON_ReplaceItemInObjectCaseSensitive(event_alert, "id", cJSON_CreateString("late-pending-event"));
    line = cJSON_PrintUnformatted(snapshot);
    assert(apply(line, 1500, &effect));
    cJSON_free(line);
    char acknowledged[AM_ID_MAX];
    assert(am_model_ack(&model, 0, acknowledged, sizeof(acknowledged)));
    cJSON_ReplaceItemInObjectCaseSensitive(snapshot, "alerts", cJSON_CreateArray());
    line = cJSON_PrintUnformatted(snapshot);
    assert(apply(line, 1600, &effect));
    cJSON_free(line);
    assert(model.snapshot.alert_count == 0 && model.pending.ack_count == 1);
    line = cJSON_PrintUnformatted(event);
    assert(!apply(line, 1700, &effect));
    assert(!effect.sound && !effect.wake && model.snapshot.alert_count == 0);
    assert(model.pending.ack_count == 1);
    cJSON_free(line);
    cJSON_Delete(event);
    cJSON_Delete(snapshot);
}

static void apply_snapshot(cJSON *snapshot, int64_t now_ms)
{
    cJSON *sequence = cJSON_GetObjectItemCaseSensitive(snapshot, "seq");
    cJSON_SetNumberValue(sequence, sequence->valuedouble + 1);
    char *line = cJSON_PrintUnformatted(snapshot);
    assert(line);
    am_effect_t effect;
    assert(apply(line, now_ms, &effect) && effect.received && !effect.sound);
    cJSON_free(line);
}

static void focus_updates(const char *fixture)
{
    am_model_init(&model);
    am_effect_t effect;
    assert(apply(fixture, 1000, &effect));
    assert(model.snapshot.task_count == 3);
    cJSON *snapshot = cJSON_Parse(fixture);
    assert(snapshot);
    cJSON *tasks = cJSON_GetObjectItemCaseSensitive(snapshot, "tasks");
    model.page = AM_PAGE_TASKS;
    model.selected = 1;
    cJSON *focused = cJSON_DetachItemFromArray(tasks, 1);
    assert(cJSON_InsertItemInArray(tasks, 0, focused));
    apply_snapshot(snapshot, 1100);
    assert(model.selected == 0 && strcmp(model.snapshot.tasks[0].client, "claude") == 0);
    (void)am_model_navigate(&model, AM_NAV_OK);
    (void)am_model_navigate(&model, AM_NAV_OK);
    assert(model.page == AM_PAGE_TASK_DETAIL && model.detail_page == 1);
    focused = cJSON_DetachItemFromArray(tasks, 0);
    assert(cJSON_AddItemToArray(tasks, focused));
    apply_snapshot(snapshot, 1200);
    assert(model.page == AM_PAGE_TASK_DETAIL && model.detail_index == 2 && model.detail_page == 1);
    assert(strcmp(model.snapshot.tasks[model.detail_index].id, "claude:ui-two:turn-seven") == 0);
    cJSON_DeleteItemFromArray(tasks, 2);
    apply_snapshot(snapshot, 1300);
    assert(model.page == AM_PAGE_TASKS && model.selected == 0 && model.detail_page == 0);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(snapshot, "tasks", cJSON_CreateArray()));
    apply_snapshot(snapshot, 1400);
    assert(model.page == AM_PAGE_TASKS && model.snapshot.task_count == 0 && model.selected == 0);
    assert(model.snapshot.running == 2 && model.snapshot.completed_today == 12);

    cJSON *quotas = cJSON_GetObjectItemCaseSensitive(snapshot, "quotas");
    model.page = AM_PAGE_QUOTAS;
    model.selected = 1;
    model.detail_page = 1;
    focused = cJSON_DetachItemFromArray(quotas, 1);
    assert(cJSON_InsertItemInArray(quotas, 0, focused));
    apply_snapshot(snapshot, 1500);
    assert(model.selected == 0 && model.detail_page == 1);
    assert(strcmp(model.snapshot.quotas[0].id, "fixture-zcode-five-hour") == 0);
    cJSON_DeleteItemFromArray(quotas, 0);
    apply_snapshot(snapshot, 1600);
    assert(model.selected == 0 && model.detail_page == 0);

    cJSON *alerts = cJSON_GetObjectItemCaseSensitive(snapshot, "alerts");
    model.page = AM_PAGE_ALERTS;
    model.selected = 0;
    focused = cJSON_DetachItemFromArray(alerts, 0);
    assert(cJSON_AddItemToArray(alerts, focused));
    apply_snapshot(snapshot, 1700);
    assert(model.selected == 2 && strcmp(model.snapshot.alerts[2].id, "ui-completed-alert") == 0);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(snapshot, "alerts", cJSON_CreateArray()));
    apply_snapshot(snapshot, 1800);
    assert(model.page == AM_PAGE_ALERTS && model.selected == 0 && model.snapshot.alert_count == 0);
    cJSON_Delete(snapshot);
}

static void reset_news(const char *fixture)
{
    cJSON *snapshot = cJSON_Parse(fixture);
    assert(snapshot);
    cJSON *news = cJSON_CreateObject();
    cJSON_AddStringToObject(news, "title", "Tibo · 重置公告待确认");
    cJSON_AddStringToObject(news, "state", "available");
    cJSON_AddStringToObject(news, "timingText", "Lands tomorrow before 10am PST.");
    cJSON_AddNumberToObject(news, "publishedAt", 1792652154);
    cJSON_AddNumberToObject(news, "checkedAt", 1792730000);
    cJSON_AddItemToObject(snapshot, "resetNews", news);
    am_model_init(&model);
    apply_snapshot(snapshot, 1000);
    assert(model.snapshot.news_published_at == 1792652154);
    assert(strcmp(model.snapshot.news_timing, "Lands tomorrow before 10am PST.") == 0);
    am_effect_t effect;
    const char *event = "{\"v\":1,\"type\":\"event\",\"sound\":true,\"event\":{\"id\":\"reset:123\",\"taskId\":\"reset:123\",\"kind\":\"reset_news\",\"title\":\"Tibo\",\"createdAt\":1792730000,\"unread\":true}}";
    assert(apply(event, 1500, &effect));
    assert(effect.wake && strcmp(effect.kind, "reset_news") == 0);
    int running = model.snapshot.running;
    assert(!apply(event, 1600, &effect));
    assert(model.snapshot.running == running);
    cJSON_Delete(snapshot);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    char *fixture = read_fixture(argv[1]);
    char *read_snapshot = read_fixture(argv[2]);
    char *queued_event = read_fixture(argv[3]);
    char *ui_snapshot = read_fixture(argv[4]);
    framing();
    quiet_hours();
    navigation();
    protocol(fixture);
    queued_events_after_snapshot(fixture, read_snapshot, queued_event);
    focus_updates(ui_snapshot);
    reset_news(fixture);
    free(ui_snapshot);
    free(queued_event);
    free(read_snapshot);
    free(fixture);
    printf("Agent Monitor protocol/model: PASS\n");
    return 0;
}
