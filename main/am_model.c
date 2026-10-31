#include "am_model.h"

#include <string.h>

void am_model_init(am_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->snapshot.quiet_start = 22 * 60;
    model->snapshot.quiet_end = 8 * 60;
}

void am_model_replace_snapshot(am_model_t *model, const am_snapshot_t *snapshot)
{
    char focused_id[AM_ID_MAX] = {0};
    bool focused = false;
    if (model->page == AM_PAGE_TASK_DETAIL && model->detail_index < model->snapshot.task_count) {
        am_utf8_copy(focused_id, sizeof(focused_id), model->snapshot.tasks[model->detail_index].id);
        focused = true;
    } else if (model->page == AM_PAGE_TASKS && model->selected < model->snapshot.task_count) {
        am_utf8_copy(focused_id, sizeof(focused_id), model->snapshot.tasks[model->selected].id);
        focused = true;
    } else if (model->page == AM_PAGE_ALERTS && model->selected < model->snapshot.alert_count) {
        am_utf8_copy(focused_id, sizeof(focused_id), model->snapshot.alerts[model->selected].id);
        focused = true;
    } else if (model->page == AM_PAGE_QUOTAS && model->selected < model->snapshot.quota_count) {
        am_utf8_copy(focused_id, sizeof(focused_id), model->snapshot.quotas[model->selected].id);
        focused = true;
    }
    model->snapshot = *snapshot;
    if (model->page == AM_PAGE_TASK_DETAIL) {
        for (size_t i = 0; focused && i < snapshot->task_count; i++) {
            if (strcmp(focused_id, snapshot->tasks[i].id) == 0) {
                model->detail_index = i;
                return;
            }
        }
        model->page = AM_PAGE_TASKS;
        model->selected = 0;
        model->detail_page = 0;
    } else if (focused) {
        size_t count = am_model_selection_count(model);
        for (size_t i = 0; i < count; i++) {
            const char *id = model->page == AM_PAGE_TASKS ? snapshot->tasks[i].id
                : model->page == AM_PAGE_ALERTS ? snapshot->alerts[i].id : snapshot->quotas[i].id;
            if (strcmp(focused_id, id) == 0) {
                model->selected = i;
                return;
            }
        }
        model->selected = 0;
        model->detail_page = 0;
    }
}

void am_utf8_copy(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) return;
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
        while (length > 0 && (((unsigned char)source[length] & 0xc0) == 0x80)) length--;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

void am_framer_feed(am_framer_t *framer, const void *bytes, size_t length,
                    am_line_cb_t callback, void *context)
{
    const unsigned char *input = bytes;
    for (size_t i = 0; i < length; i++) {
        if (input[i] == '\n') {
            if (!framer->dropping && framer->length > 0) {
                framer->bytes[framer->length] = '\0';
                callback(framer->bytes, framer->length, context);
            }
            framer->length = 0;
            framer->dropping = false;
        } else if (!framer->dropping && input[i] != '\r') {
            if (framer->length == AM_LINE_MAX || input[i] == '\0') {
                framer->dropping = true;
                framer->length = 0;
            } else {
                framer->bytes[framer->length++] = (char)input[i];
            }
        }
    }
}

bool am_model_online(const am_model_t *model, int64_t now_ms)
{
    return model->has_snapshot && now_ms >= model->received_ms
        && now_ms - model->received_ms <= AM_STALE_MS;
}

unsigned am_model_local_minute(const am_model_t *model, int64_t now_ms)
{
    int64_t elapsed = now_ms > model->received_ms ? now_ms - model->received_ms : 0;
    return (model->snapshot.local_minute + (unsigned)(elapsed / 60000)) % 1440;
}

bool am_quiet(unsigned minute, unsigned start, unsigned end)
{
    if (start == end) return false;
    return start < end ? minute >= start && minute < end : minute >= start || minute < end;
}

bool am_model_ack(am_model_t *model, size_t index, char *id, size_t capacity)
{
    if (index >= model->snapshot.alert_count || !model->snapshot.alerts[index].unread) return false;
    if (model->pending.ack_count >= AM_ALERT_MAX) return false;
    model->snapshot.alerts[index].unread = false;
    am_utf8_copy(id, capacity, model->snapshot.alerts[index].id);
    am_utf8_copy(model->pending.ack_ids[model->pending.ack_count++], AM_ID_MAX, model->snapshot.alerts[index].id);
    return true;
}

void am_model_set_mute(am_model_t *model, bool muted)
{
    model->snapshot.muted = muted;
    model->pending.has_mute = true;
    model->pending.muted = muted;
}

bool am_model_ack_pending(const am_model_t *model, const char *id)
{
    for (size_t i = 0; i < model->pending.ack_count; i++) {
        if (strcmp(model->pending.ack_ids[i], id) == 0) return true;
    }
    return false;
}

size_t am_model_selection_count(const am_model_t *model)
{
    switch (model->page) {
    case AM_PAGE_OVERVIEW: return 5;
    case AM_PAGE_TASKS: return model->snapshot.task_count;
    case AM_PAGE_ALERTS: return model->snapshot.alert_count;
    case AM_PAGE_QUOTAS: return model->snapshot.quota_count;
    case AM_PAGE_SETTINGS: return 4;
    case AM_PAGE_TASK_DETAIL: return 2;
    case AM_PAGE_NEWS: return 0;
    }
    return 0;
}

am_action_t am_model_navigate(am_model_t *model, am_navigation_t navigation)
{
    const am_page_t pages[] = {AM_PAGE_TASKS, AM_PAGE_ALERTS, AM_PAGE_NEWS, AM_PAGE_QUOTAS, AM_PAGE_SETTINGS};
    if (navigation == AM_NAV_BACK) {
        if (model->page == AM_PAGE_TASK_DETAIL) {
            model->page = AM_PAGE_TASKS;
            model->selected = model->detail_index;
        } else if (model->page != AM_PAGE_OVERVIEW) {
            for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
                if (model->page == pages[i]) {
                    model->selected = i;
                    break;
                }
            }
            model->page = AM_PAGE_OVERVIEW;
        }
        return AM_ACTION_NONE;
    }
    size_t count = am_model_selection_count(model);
    if (navigation == AM_NAV_UP || navigation == AM_NAV_DOWN) {
        if (model->page == AM_PAGE_TASK_DETAIL) model->detail_page = 1 - model->detail_page;
        else if (count > 0) {
            model->selected = navigation == AM_NAV_UP
                ? (model->selected + count - 1) % count : (model->selected + 1) % count;
            if (model->page == AM_PAGE_QUOTAS) model->detail_page = 0;
        }
        return AM_ACTION_NONE;
    }
    if (model->page == AM_PAGE_OVERVIEW) {
        model->page = pages[model->selected % 5];
        model->selected = 0;
        model->detail_page = 0;
    } else if (model->page == AM_PAGE_TASKS && count > 0) {
        model->detail_index = model->selected % count;
        model->detail_page = 0;
        model->page = AM_PAGE_TASK_DETAIL;
    } else if (model->page == AM_PAGE_QUOTAS) {
        model->detail_page = (model->detail_page + 1) % 3;
    } else if (model->page == AM_PAGE_TASK_DETAIL) {
        model->detail_page = 1 - model->detail_page;
    } else if (model->page == AM_PAGE_ALERTS && count > 0) {
        return AM_ACTION_ACK;
    } else if (model->page == AM_PAGE_SETTINGS) {
        switch (model->selected) {
        case 0: return AM_ACTION_MUTE;
        case 1: return AM_ACTION_BRIGHTNESS;
        case 2: return AM_ACTION_TIMEOUT;
        default: (void)am_model_navigate(model, AM_NAV_BACK); break;
        }
    }
    return AM_ACTION_NONE;
}

size_t am_model_unread(const am_model_t *model)
{
    size_t unread = 0;
    for (size_t i = 0; i < model->snapshot.alert_count; i++) {
        if (model->snapshot.alerts[i].unread) unread++;
    }
    return unread;
}
