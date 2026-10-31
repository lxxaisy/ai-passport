#include "am_view.h"

#include "lvgl.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

LV_FONT_DECLARE(am_font_16);

#define INK 0xF0F4F1
#define MUTED 0xA8B7AC
#define ACCENT 0x95C5F3
#define INFO 0x84CEB3
#define WARNING 0xF0C17A
#define DANGER 0xED9690
#define BG 0x171F1B
#define SELECTED 0x35483D
#define RULE 0x384B3E

static lv_obj_t *s_screen;
static lv_obj_t *s_labels[32];
static lv_obj_t *s_shapes[8];
static int s_render_key = -1;

static lv_obj_t *put(size_t index, int x, int y, int width, int height,
                      const lv_font_t *font, const char *text, uint32_t color)
{
    if (!s_labels[index]) {
        s_labels[index] = lv_label_create(s_screen);
        lv_obj_set_style_text_line_space(s_labels[index], 0, 0);
        lv_label_set_long_mode(s_labels[index], LV_LABEL_LONG_DOT);
    }
    lv_obj_t *object = s_labels[index];
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    if (strcmp(lv_label_get_text(object), text) != 0) lv_label_set_text(object, text);
    return object;
}

static void right(lv_obj_t *object)
{
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, 0);
}

static lv_obj_t *shape(size_t index, int x, int y, int width, int height, uint32_t color)
{
    if (!s_shapes[index]) {
        s_shapes[index] = lv_obj_create(s_screen);
        lv_obj_remove_style_all(s_shapes[index]);
        lv_obj_remove_flag(s_shapes[index], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_background(s_shapes[index]);
    }
    lv_obj_t *object = s_shapes[index];
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static void focus(int y, int height, uint32_t color)
{
    lv_obj_t *object = shape(2, 10, y, 220, height, SELECTED);
    lv_obj_set_style_border_side(object, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_40, 0);
    lv_obj_set_style_radius(object, 8, 0);
}

static const char *status_label(const char *status)
{
    if (strcmp(status, "running") == 0) return "运行中";
    if (strcmp(status, "needs_action") == 0) return "待处理";
    if (strcmp(status, "succeeded") == 0) return "已完成";
    if (strcmp(status, "failed") == 0) return "失败";
    if (strcmp(status, "interrupted") == 0) return "已中断";
    return "未知";
}

static uint32_t status_color(const char *status)
{
    if (strcmp(status, "running") == 0) return ACCENT;
    if (strcmp(status, "needs_action") == 0) return WARNING;
    if (strcmp(status, "failed") == 0) return DANGER;
    if (strcmp(status, "succeeded") == 0) return INFO;
    return MUTED;
}

static const char *client_label(const char *client)
{
    if (strcmp(client, "codex") == 0) return "Codex";
    if (strcmp(client, "claude") == 0) return "Claude Code";
    if (strcmp(client, "zcode") == 0) return "ZCode";
    return client;
}

static const char *kind_label(const char *kind)
{
    if (strcmp(kind, "reset_news") == 0) return "重置消息";
    if (strcmp(kind, "completed") == 0) return "已完成";
    if (strcmp(kind, "failed") == 0) return "失败";
    return "待处理";
}

static uint32_t kind_color(const char *kind)
{
    if (strcmp(kind, "completed") == 0) return INFO;
    return strcmp(kind, "failed") == 0 ? DANGER : WARNING;
}

static const char *quota_state(const char *state)
{
    if (strcmp(state, "available") == 0) return "已同步";
    if (strcmp(state, "unavailable") == 0) return "未提供";
    if (strcmp(state, "unsupported") == 0) return "不支持";
    if (strcmp(state, "stale") == 0) return "已过期";
    if (strcmp(state, "error") == 0) return "读取失败";
    return "未知";
}

static void duration(char *out, size_t size, int64_t seconds)
{
    if (seconds < 0) snprintf(out, size, "未提供");
    else if (seconds < 3600) snprintf(out, size, "%02" PRId64 ":%02" PRId64, seconds / 60, seconds % 60);
    else snprintf(out, size, "%" PRId64 "h %02" PRId64 "m", seconds / 3600, seconds / 60 % 60);
}

static void amount(char *out, size_t size, int64_t value)
{
    if (value < 0) snprintf(out, size, "未提供");
    else if (value >= 1000000) snprintf(out, size, "%.1fM", value / 1000000.0);
    else if (value >= 10000) snprintf(out, size, "%.1fk", value / 1000.0);
    else snprintf(out, size, "%" PRId64, value);
}

static void age(char *out, size_t size, int64_t seconds)
{
    if (seconds < 60) snprintf(out, size, "%" PRId64 "s", seconds);
    else if (seconds < 3600) snprintf(out, size, "%" PRId64 "m %02" PRId64 "s", seconds / 60, seconds % 60);
    else snprintf(out, size, "%" PRId64 "h %02" PRId64 "m", seconds / 3600, seconds / 60 % 60);
}

static int64_t elapsed_seconds(const am_model_t *model, int64_t now_ms)
{
    return model->has_snapshot && now_ms > model->received_ms ? (now_ms - model->received_ms) / 1000 : 0;
}

static void header(const am_model_t *model, int64_t now_ms, int battery)
{
    char buffer[80];
    put(0, 18, 12, 158, 20, &lv_font_montserrat_14, "AGENT MONITOR", INK);
    if (battery >= 0) snprintf(buffer, sizeof(buffer), "%d%%", battery);
    else snprintf(buffer, sizeof(buffer), "--");
    right(put(1, 181, 12, 42, 20, &lv_font_montserrat_14, buffer, MUTED));
    bool online = am_model_online(model, now_ms);
    if (!model->has_snapshot) snprintf(buffer, sizeof(buffer), "等待电脑同步");
    else snprintf(buffer, sizeof(buffer), "%s · %s", model->transport == AM_USB ? "USB" : "BLE",
        online ? "同步正常" : "同步超时");
    put(2, 18, 34, 154, 20, &am_font_16, buffer, online ? ACCENT : WARNING);
    if (model->has_snapshot) {
        int64_t seconds = elapsed_seconds(model, now_ms);
        if (seconds < 60) snprintf(buffer, sizeof(buffer), "%" PRId64 "s", seconds);
        else if (seconds < 3600) snprintf(buffer, sizeof(buffer), "%" PRId64 "m", seconds / 60);
        else snprintf(buffer, sizeof(buffer), "%" PRId64 "h", seconds / 3600);
    } else snprintf(buffer, sizeof(buffer), "--");
    right(put(3, 174, 36, 49, 18, &lv_font_montserrat_14, buffer, MUTED));
    shape(0, 18, 57, 204, 1, RULE);
}

static void section(const char *title, const char *position)
{
    put(4, 18, 64, 150, 20, &am_font_16, title, INK);
    if (position[0]) right(put(5, 174, 66, 49, 18, &lv_font_montserrat_14, position, MUTED));
    else if (s_labels[5]) lv_label_set_text(s_labels[5], "");
}

static void footer(const char *actions, bool back)
{
    shape(1, 18, 277, 204, 1, RULE);
    lv_obj_t *object = put(30, 18, 280, 204, 20, &am_font_16, actions, INK);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    if (back) {
        object = put(31, 30, 300, 180, 20, &am_font_16, "长按 OK 返回", MUTED);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void overview(const am_model_t *model, int64_t now_ms)
{
    section("工作总览", "");
    char buffer[80];
    const int values[] = {model->snapshot.running, model->snapshot.needs_action, model->snapshot.completed_today};
    const char *names[] = {"运行中", "待处理", "今日完成"};
    const uint32_t colors[] = {ACCENT, WARNING, INK};
    for (size_t i = 0; i < 3; i++) {
        int x = 18 + (int)i * 70;
        if (model->has_snapshot) snprintf(buffer, sizeof(buffer), "%d", values[i]);
        else snprintf(buffer, sizeof(buffer), "--");
        put(6 + i, x, 92, 64, 27, &lv_font_montserrat_20, buffer, colors[i]);
        put(9 + i, x, 121, 67, 20, &am_font_16, names[i], MUTED);
    }
    const char *items[] = {"近期任务", "近期提醒", "Tibo 公告", "账户额度", "设备设置"};
    for (size_t i = 0; i < 5; i++) {
        int y = 146 + (int)i * 25;
        if (model->selected == i) focus(y, 24, i == 2 ? WARNING : ACCENT);
        if (i == 2) {
            snprintf(buffer, sizeof(buffer), "Tibo 公告 · %s", model->snapshot.news_title[0] ? "重置消息" : "等待同步");
            put(12 + i * 2, 20, y + 2, 202, 20, &am_font_16, buffer, model->snapshot.news_title[0] ? WARNING : MUTED);
            continue;
        }
        put(12 + i * 2, 20, y + 2, 112, 20, &am_font_16, items[i], model->selected == i ? INK : MUTED);
        if (i == 0) snprintf(buffer, sizeof(buffer), "%u 条", (unsigned)model->snapshot.task_count);
        else if (i == 1) snprintf(buffer, sizeof(buffer), "%u 未读", (unsigned)am_model_unread(model));
        else if (i == 2) snprintf(buffer, sizeof(buffer), "%s", model->snapshot.news_title[0] ? "重置消息" : "等待同步");
        else if (i == 3) snprintf(buffer, sizeof(buffer), "%u 窗口", (unsigned)model->snapshot.quota_count);
        else snprintf(buffer, sizeof(buffer), "%s", model->snapshot.muted ? "静音"
            : model->has_snapshot && am_quiet(am_model_local_minute(model, now_ms), model->snapshot.quiet_start, model->snapshot.quiet_end)
            ? "夜间静音" : "提示音开");
        right(put(13 + i * 2, 135, y + 2, 85, 20, &am_font_16, buffer,
            (i == 1 && am_model_unread(model)) || (i == 2 && model->snapshot.news_title[0]) ? WARNING : MUTED));
    }
    footer("↑↓ 选择 · OK 进入", false);
}

static void tasks(const am_model_t *model)
{
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%u/%u", model->snapshot.task_count ? (unsigned)model->selected + 1 : 0,
        (unsigned)model->snapshot.task_count);
    section("近期任务", buffer);
    if (model->snapshot.task_count == 0) {
        put(6, 18, 136, 204, 40, &am_font_16, model->has_snapshot ? "暂无任务" : "等待同步任务", MUTED);
    } else {
        size_t start = model->selected / 3 * 3;
        for (size_t row = 0; row < 3; row++) {
            size_t index = start + row;
            int y = 89 + (int)row * 61;
            if (index == model->selected) focus(y, 60, ACCENT);
            if (index >= model->snapshot.task_count) {
                for (size_t line = 0; line < 3; line++) put(6 + row * 3 + line,
                    20, y + (int)line * 20, 202, 20, &am_font_16, "", MUTED);
                continue;
            }
            const am_task_t *task = &model->snapshot.tasks[index];
            put(6 + row * 3, 20, y, 202, 20, &am_font_16, task->title, INK);
            snprintf(buffer, sizeof(buffer), "%s · %s", client_label(task->client), status_label(task->status));
            put(7 + row * 3, 20, y + 20, 202, 20, &am_font_16, buffer, status_color(task->status));
            put(8 + row * 3, 20, y + 40, 202, 20, &am_font_16, task->project, MUTED);
        }
    }
    footer("↑↓ 选择 · OK 详情", true);
}

static void token_row(size_t row, const char *name, int64_t value)
{
    char buffer[32];
    amount(buffer, sizeof(buffer), value);
    int y = 113 + (int)row * 24;
    put(8 + row * 2, 18, y, 136, 20, &am_font_16, name, row == 5 ? INK : MUTED);
    right(put(9 + row * 2, 154, y, 68, 20, &am_font_16, buffer, row == 5 ? INFO : INK));
}

static void task_detail(const am_model_t *model, int64_t now_ms)
{
    if (model->detail_index >= model->snapshot.task_count) return;
    const am_task_t *task = &model->snapshot.tasks[model->detail_index];
    section(model->detail_page == 0 ? "任务详情" : "分类 Token", model->detail_page == 0 ? "1/2" : "2/2");
    char buffer[160], elapsed[40], fresh[48];
    put(6, 18, 88, 204, model->detail_page == 0 ? 40 : 20, &am_font_16, task->title, INK);
    if (model->detail_page == 0) {
        snprintf(buffer, sizeof(buffer), "%s · %s", client_label(task->client), status_label(task->status));
        put(7, 18, 133, 204, 20, &am_font_16, buffer, status_color(task->status));
        put(8, 18, 156, 204, 20, &am_font_16, task->project, MUTED);
        int64_t seconds = task->duration_seconds;
        if (seconds >= 0 && strcmp(task->status, "running") == 0 && am_model_online(model, now_ms)) {
            seconds += elapsed_seconds(model, now_ms);
        }
        duration(elapsed, sizeof(elapsed), seconds);
        put(9, 18, 188, 88, 20, &am_font_16, "墙钟耗时", MUTED);
        right(put(10, 109, 186, 113, 27, seconds >= 0 ? &lv_font_montserrat_20 : &am_font_16, elapsed, INK));
        snprintf(buffer, sizeof(buffer), "第 %d 轮 · 子任务 %d", task->turn_index, task->child_count);
        put(11, 18, 219, 204, 20, &am_font_16, buffer, MUTED);
        if (task->observed_at >= 0) {
            age(fresh, sizeof(fresh), model->snapshot.sent_at + elapsed_seconds(model, now_ms) - task->observed_at);
            snprintf(buffer, sizeof(buffer), "来源更新 %s 前", fresh);
        } else snprintf(buffer, sizeof(buffer), "来源时间未提供");
        put(12, 18, 247, 204, 20, &am_font_16, buffer, MUTED);
    } else {
        token_row(0, task->tokens.input_includes_cache ? "输入（含缓存）" : "输入", task->tokens.input);
        token_row(1, "输出", task->tokens.output);
        token_row(2, "缓存读取", task->tokens.cache_read);
        token_row(3, "缓存写入", task->tokens.cache_write);
        token_row(4, task->tokens.output_includes_reasoning ? "推理（含于输出）" : "推理", task->tokens.reasoning);
        token_row(5, "总计", task->tokens.total);
        shape(6, 18, 230, 204, 1, RULE);
    }
    footer(model->detail_page == 0 ? "↑↓ 翻页 · OK 用量" : "↑↓ 翻页 · OK 详情", true);
}

static void alerts(const am_model_t *model, int64_t now_ms)
{
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%u/%u", model->snapshot.alert_count ? (unsigned)model->selected + 1 : 0,
        (unsigned)model->snapshot.alert_count);
    section("近期提醒", buffer);
    if (model->snapshot.alert_count == 0) {
        put(6, 18, 136, 204, 40, &am_font_16, "暂无新提醒", MUTED);
    } else {
        size_t start = model->selected / 3 * 3;
        for (size_t row = 0; row < 3; row++) {
            size_t index = start + row;
            int y = 89 + (int)row * 61;
            if (index == model->selected) focus(y, 60, INFO);
            if (index >= model->snapshot.alert_count) {
                for (size_t line = 0; line < 3; line++) put(6 + row * 3 + line,
                    20, y + (int)line * 20, 202, 20, &am_font_16, "", MUTED);
                continue;
            }
            const am_alert_t *alert = &model->snapshot.alerts[index];
            put(6 + row * 3, 20, y, 202, 20, &am_font_16, alert->title, INK);
            snprintf(buffer, sizeof(buffer), "%s · %s", kind_label(alert->kind),
                am_model_ack_pending(model, alert->id) ? "已读待同步" : alert->unread ? "未读" : "已读");
            put(7 + row * 3, 20, y + 20, 202, 20, &am_font_16, buffer, kind_color(alert->kind));
            char time[48];
            age(time, sizeof(time), model->snapshot.sent_at + elapsed_seconds(model, now_ms) - alert->created_at);
            snprintf(buffer, sizeof(buffer), "%s 前", time);
            put(8 + row * 3, 20, y + 40, 202, 20, &am_font_16, buffer, MUTED);
        }
    }
    footer("↑↓ 选择 · OK 已读", true);
}

static void quota(const am_model_t *model, int64_t now_ms)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%u/%u", model->snapshot.quota_count ? (unsigned)model->selected + 1 : 0,
        (unsigned)model->snapshot.quota_count);
    section(model->page == AM_PAGE_NEWS ? "Tibo 公告" : model->detail_page == 0 ? "额度概览" : model->detail_page == 1 ? "额度来源" : "Tibo 公告", model->page == AM_PAGE_NEWS ? "" : buffer);
    if (model->page == AM_PAGE_NEWS || model->detail_page == 2) {
        section("Tibo 公告", "");
        put(6, 18, 88, 204, 40, &am_font_16, model->snapshot.news_title[0] ? model->snapshot.news_title : "尚无重置公告", INK);
        put(7, 18, 133, 204, 20, &am_font_16, "Codex · 第三方转录", MUTED);
        if (model->snapshot.news_published_at >= 0) {
            time_t epoch = (time_t)(model->snapshot.news_published_at + 8 * 3600);
            struct tm time;
            if (gmtime_r(&epoch, &time)) strftime(buffer, sizeof(buffer), "%m-%d %H:%M UTC+8", &time);
            else snprintf(buffer, sizeof(buffer), "发布时间未提供");
        } else snprintf(buffer, sizeof(buffer), "发布时间未提供");
        put(8, 18, 157, 204, 20, &am_font_16, buffer, MUTED);
        put(9, 18, 185, 204, 60, &am_font_16, model->snapshot.news_timing, INK);
        const char *state = strcmp(model->snapshot.news_state, "available") == 0 ? "公告不适用于 BigModel"
            : strcmp(model->snapshot.news_state, "disabled") == 0 ? "公告检查已暂停" : "公告来源未更新";
        put(10, 18, 249, 204, 20, &am_font_16, state, WARNING);
        footer(model->page == AM_PAGE_NEWS ? "长按 OK 返回" : "↑↓ 账户 · OK 额度", model->page != AM_PAGE_NEWS);
        return;
    }
    if (model->snapshot.quota_count == 0) {
        put(6, 18, 136, 204, 40, &am_font_16, "尚未取得账户额度", MUTED);
        footer("↑↓ 账户", true);
        return;
    }
    const am_quota_t *quota = &model->snapshot.quotas[model->selected % model->snapshot.quota_count];
    int64_t epoch = model->snapshot.sent_at + elapsed_seconds(model, now_ms);
    put(6, 18, 88, 204, 40, &am_font_16, quota->label, INK);
    if (model->detail_page == 0) {
        put(7, 18, 134, 80, 20, &am_font_16, "剩余", MUTED);
        if (quota->used_percent >= 0) snprintf(buffer, sizeof(buffer), "%.1f%%", 100 - quota->used_percent);
        else snprintf(buffer, sizeof(buffer), "未提供");
        right(put(8, 104, 132, 118, 27, quota->used_percent >= 0 ? &lv_font_montserrat_20 : &am_font_16,
            buffer, quota->used_percent >= 0 ? INFO : WARNING));
        shape(6, 18, 168, 204, 5, RULE);
        int width = quota->used_percent >= 0 ? (int)(204 * (100 - quota->used_percent) / 100) : 0;
        lv_obj_t *fill = shape(7, 18, 168, width > 0 ? width : 1, 5, INFO);
        lv_obj_set_style_bg_opa(fill, width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_move_foreground(fill);
        if (quota->window_minutes < 0) snprintf(buffer, sizeof(buffer), "窗口未提供");
        else if (quota->window_minutes > 0 && quota->window_minutes % 1440 == 0) {
            snprintf(buffer, sizeof(buffer), "%d 天窗口", quota->window_minutes / 1440);
        } else if (quota->window_minutes > 0 && quota->window_minutes % 60 == 0) {
            snprintf(buffer, sizeof(buffer), "%d 小时窗口", quota->window_minutes / 60);
        } else snprintf(buffer, sizeof(buffer), "%d 分钟窗口", quota->window_minutes);
        put(9, 18, 185, 204, 20, &am_font_16, buffer, INK);
        if (quota->resets_at < 0) snprintf(buffer, sizeof(buffer), "重置时间未提供");
        else if (quota->resets_at <= epoch) snprintf(buffer, sizeof(buffer), "已到重置时间 · 待刷新");
        else {
            char time[40];
            duration(time, sizeof(time), quota->resets_at - epoch);
            snprintf(buffer, sizeof(buffer), "距重置 %s", time);
        }
        put(10, 18, 211, 204, 20, &am_font_16, buffer, MUTED);
        put(11, 18, 240, 204, 20, &am_font_16, quota_state(quota->state),
            strcmp(quota->state, "available") == 0 ? ACCENT : WARNING);
    } else {
        snprintf(buffer, sizeof(buffer), "%s · %s", quota->provider, quota_state(quota->state));
        put(7, 18, 134, 204, 20, &am_font_16, buffer, MUTED);
        if (quota->observed_at >= 0) {
            char time[48];
            age(time, sizeof(time), epoch - quota->observed_at);
            snprintf(buffer, sizeof(buffer), "来源更新 %s 前", time);
        } else snprintf(buffer, sizeof(buffer), "来源时间未提供");
        put(8, 18, 163, 204, 20, &am_font_16, buffer, MUTED);
        if (quota->resets_at >= 0) {
            time_t epoch = (time_t)(quota->resets_at + 8 * 3600);
            struct tm time;
            if (gmtime_r(&epoch, &time)) strftime(buffer, sizeof(buffer), "%m-%d %H:%M UTC+8", &time);
            else snprintf(buffer, sizeof(buffer), "重置时间未提供");
        } else snprintf(buffer, sizeof(buffer), "重置时间未提供");
        put(9, 18, 189, 204, 20, &am_font_16, buffer, INK);
        put(10, 18, 219, 204, 40, &am_font_16, quota->detail, MUTED);
    }
    footer(model->detail_page == 0 ? "↑↓ 账户 · OK 来源" : "↑↓ 账户 · OK 公告", true);
}

static void settings(const am_model_t *model, unsigned brightness, unsigned timeout)
{
    section("设备设置", "");
    const char *names[] = {"提示音", "屏幕亮度", "自动息屏", "返回总览"};
    char buffer[128];
    for (size_t i = 0; i < 4; i++) {
        int y = 91 + (int)i * 39;
        if (model->selected == i) focus(y, 36, ACCENT);
        put(6 + i * 2, 20, y + 8, 70, 20, &am_font_16, names[i], model->selected == i ? INK : MUTED);
        if (i == 0) snprintf(buffer, sizeof(buffer), "%s%s", model->snapshot.muted ? "静音" : "开启",
            model->pending.has_mute ? "·待同步" : "");
        else if (i == 1) snprintf(buffer, sizeof(buffer), "%u%%", brightness);
        else if (i == 2 && timeout > 0) snprintf(buffer, sizeof(buffer), "%u 秒", timeout);
        else if (i == 2) snprintf(buffer, sizeof(buffer), "不息屏");
        else buffer[0] = '\0';
        right(put(7 + i * 2, 98, y + 8, 124, 20, &am_font_16, buffer, MUTED));
    }
    snprintf(buffer, sizeof(buffer), "静音 %02u:%02u-%02u:%02u", model->snapshot.quiet_start / 60,
        model->snapshot.quiet_start % 60, model->snapshot.quiet_end / 60, model->snapshot.quiet_end % 60);
    put(14, 18, 253, 204, 20, &am_font_16, buffer, MUTED);
    footer("↑↓ 选择 · OK 修改", true);
}

void am_view_render(const am_model_t *model, int64_t now_ms, int battery,
                     unsigned brightness, unsigned timeout, uint32_t pairing_code,
                     const am_alert_t *notice)
{
    if (!s_screen) {
        s_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_screen, lv_color_hex(BG), 0);
        lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_screen, 0, 0);
        lv_obj_set_style_pad_all(s_screen, 0, 0);
        lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_screen_load(s_screen);
    }
    size_t count = model->page == AM_PAGE_TASKS ? model->snapshot.task_count
        : model->page == AM_PAGE_ALERTS ? model->snapshot.alert_count
        : model->page == AM_PAGE_QUOTAS ? model->snapshot.quota_count : 0;
    int detail = model->page == AM_PAGE_TASK_DETAIL || model->page == AM_PAGE_QUOTAS ? (int)model->detail_page : 0;
    int key = pairing_code ? 99999 : notice ? 99998 : (int)model->page * 1000 + (int)count * 10 + detail;
    if (s_render_key != key) {
        lv_obj_clean(s_screen);
        memset(s_labels, 0, sizeof(s_labels));
        memset(s_shapes, 0, sizeof(s_shapes));
        s_render_key = key;
    }
    header(model, now_ms, battery);
    if (pairing_code) {
        section("蓝牙安全配对", "");
        char number[24];
        snprintf(number, sizeof(number), "%06" PRIu32, pairing_code);
        put(6, 18, 115, 204, 20, &am_font_16, "配对码", MUTED);
        lv_obj_t *code = put(7, 18, 154, 204, 30, &lv_font_montserrat_20, number, INFO);
        lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
        put(8, 18, 214, 204, 40, &am_font_16, "在 Mac 输入此号码", INK);
        footer("等待 Mac 确认", false);
        return;
    }
    if (notice) {
        section("新提醒", "");
        put(6, 18, 101, 204, 20, &am_font_16, kind_label(notice->kind), kind_color(notice->kind));
        put(7, 18, 143, 204, 80, &am_font_16, notice->title, INK);
        put(8, 18, 247, 204, 20, &am_font_16, "未读", WARNING);
        footer("↑↓ 关闭 · OK 已读", false);
        return;
    }
    switch (model->page) {
    case AM_PAGE_OVERVIEW: overview(model, now_ms); break;
    case AM_PAGE_TASKS: tasks(model); break;
    case AM_PAGE_TASK_DETAIL: task_detail(model, now_ms); break;
    case AM_PAGE_ALERTS: alerts(model, now_ms); break;
    case AM_PAGE_QUOTAS: quota(model, now_ms); break;
    case AM_PAGE_NEWS: quota(model, now_ms); break;
    case AM_PAGE_SETTINGS: settings(model, brightness, timeout); break;
    }
}
