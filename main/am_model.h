#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AM_LINE_MAX 8192
#define AM_TASK_MAX 8
#define AM_QUOTA_MAX 6
#define AM_ALERT_MAX 12
#define AM_ID_MAX 192
#define AM_TITLE_MAX 192
#define AM_PROJECT_MAX 96
#define AM_STALE_MS 20000

typedef enum { AM_USB = 0, AM_BLE = 1 } am_transport_t;
typedef enum {
    AM_PAGE_OVERVIEW = 0,
    AM_PAGE_TASKS,
    AM_PAGE_TASK_DETAIL,
    AM_PAGE_ALERTS,
    AM_PAGE_QUOTAS,
    AM_PAGE_SETTINGS,
    AM_PAGE_NEWS,
} am_page_t;

typedef enum { AM_NAV_UP, AM_NAV_DOWN, AM_NAV_OK, AM_NAV_BACK } am_navigation_t;
typedef enum {
    AM_ACTION_NONE,
    AM_ACTION_ACK,
    AM_ACTION_MUTE,
    AM_ACTION_BRIGHTNESS,
    AM_ACTION_TIMEOUT,
} am_action_t;

typedef struct {
    int64_t input;
    int64_t output;
    int64_t cache_read;
    int64_t cache_write;
    int64_t reasoning;
    int64_t total;
    bool input_includes_cache;
    bool output_includes_reasoning;
} am_tokens_t;

typedef struct {
    char id[AM_ID_MAX];
    char client[24];
    char title[AM_TITLE_MAX];
    char project[AM_PROJECT_MAX];
    char status[24];
    int turn_index;
    int child_count;
    int64_t duration_seconds;
    int64_t observed_at;
    am_tokens_t tokens;
} am_task_t;

typedef struct {
    char id[AM_ID_MAX];
    char label[96];
    char provider[48];
    char state[24];
    char detail[192];
    int window_minutes;
    double used_percent;
    int64_t resets_at;
    int64_t observed_at;
} am_quota_t;

typedef struct {
    char id[AM_ID_MAX];
    char task_id[AM_ID_MAX];
    char kind[24];
    char title[AM_TITLE_MAX];
    int64_t created_at;
    bool unread;
} am_alert_t;

typedef struct {
    uint64_t sequence;
    int64_t sent_at;
    unsigned local_minute;
    int running;
    int needs_action;
    int completed_today;
    am_task_t tasks[AM_TASK_MAX];
    size_t task_count;
    am_quota_t quotas[AM_QUOTA_MAX];
    size_t quota_count;
    am_alert_t alerts[AM_ALERT_MAX];
    size_t alert_count;
    char news_title[128];
    char news_state[24];
    char news_timing[256];
    int64_t news_published_at;
    int64_t news_checked_at;
    bool muted;
    unsigned quiet_start;
    unsigned quiet_end;
} am_snapshot_t;

typedef struct {
    char ack_ids[AM_ALERT_MAX][AM_ID_MAX];
    size_t ack_count;
    bool has_mute;
    bool muted;
} am_pending_t;

typedef struct {
    am_snapshot_t snapshot;
    am_pending_t pending;
    bool has_snapshot;
    int64_t received_ms;
    am_transport_t transport;
    am_page_t page;
    size_t selected;
    size_t detail_index;
    unsigned detail_page;
    char event_ids[AM_ALERT_MAX][AM_ID_MAX];
    size_t event_cursor;
} am_model_t;

typedef struct {
    bool redraw;
    bool wake;
    bool sound;
    bool settings_changed;
    bool hello;
    bool received;
    bool pending_changed;
    char kind[24];
    char alert_id[AM_ID_MAX];
} am_effect_t;

typedef struct {
    char bytes[AM_LINE_MAX + 1];
    size_t length;
    bool dropping;
} am_framer_t;

typedef void (*am_line_cb_t)(const char *line, size_t length, void *context);

void am_model_init(am_model_t *model);
void am_model_replace_snapshot(am_model_t *model, const am_snapshot_t *snapshot);
void am_utf8_copy(char *destination, size_t capacity, const char *source);
bool am_protocol_apply(am_model_t *model, const char *line, size_t length,
                       am_transport_t transport, int64_t now_ms, am_effect_t *effect);
void am_framer_feed(am_framer_t *framer, const void *bytes, size_t length,
                    am_line_cb_t callback, void *context);
bool am_model_online(const am_model_t *model, int64_t now_ms);
unsigned am_model_local_minute(const am_model_t *model, int64_t now_ms);
bool am_quiet(unsigned minute, unsigned start, unsigned end);
bool am_model_ack(am_model_t *model, size_t index, char *id, size_t capacity);
void am_model_set_mute(am_model_t *model, bool muted);
size_t am_model_unread(const am_model_t *model);
bool am_model_ack_pending(const am_model_t *model, const char *id);
size_t am_model_selection_count(const am_model_t *model);
am_action_t am_model_navigate(am_model_t *model, am_navigation_t navigation);
