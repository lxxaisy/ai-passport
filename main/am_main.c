#include "am_model.h"
#include "am_transport.h"
#include "am_view.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "agent_monitor";

typedef enum { MESSAGE_LINE, MESSAGE_KEY, MESSAGE_PAIR } message_kind_t;
typedef struct {
    message_kind_t kind;
    am_transport_t transport;
    char *line;
    size_t length;
    bsp_btn_t key;
    bsp_btn_ev_t key_event;
    uint32_t passkey;
} app_message_t;

static am_model_t s_model;
static QueueHandle_t s_messages;
static QueueHandle_t s_audio;
static nvs_handle_t s_preferences;
static bool s_preferences_ready;
static bool s_audio_ready;
static bool s_asleep;
static unsigned s_brightness = 70;
static unsigned s_screen_timeout = 20;
static int64_t s_last_interaction;
static int64_t s_suppress_keys_until;
static uint32_t s_passkey;
static int64_t s_passkey_until;
static am_alert_t s_notice;
static int64_t s_notice_until;

static void send_json(am_transport_t transport, cJSON *object)
{
    if (!object) return;
    char *line = cJSON_PrintUnformatted(object);
    if (line) {
        am_transport_send(transport, line);
        cJSON_free(line);
    }
    cJSON_Delete(object);
}

static cJSON *message(const char *type)
{
    cJSON *object = cJSON_CreateObject();
    if (object) {
        cJSON_AddNumberToObject(object, "v", 1);
        cJSON_AddStringToObject(object, "type", type);
    }
    return object;
}

static void send_hello(am_transport_t transport)
{
    cJSON *object = message("hello");
    if (!object) return;
    cJSON_AddStringToObject(object, "device", "ai-passport");
    cJSON_AddStringToObject(object, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(object, "transport", transport == AM_USB ? "usb" : "ble");
    send_json(transport, object);
}

static void send_pending(void)
{
    if (!s_model.has_snapshot) return;
    for (size_t i = 0; i < s_model.pending.ack_count; i++) {
        cJSON *object = message("ack");
        if (object) cJSON_AddStringToObject(object, "id", s_model.pending.ack_ids[i]);
        send_json(s_model.transport, object);
    }
    if (s_model.pending.has_mute) {
        cJSON *object = message("mute");
        if (object) cJSON_AddBoolToObject(object, "muted", s_model.pending.muted);
        send_json(s_model.transport, object);
    }
}

static void save_preferences(void)
{
    if (!s_preferences_ready) return;
    (void)nvs_set_u8(s_preferences, "muted", s_model.snapshot.muted);
    (void)nvs_set_u16(s_preferences, "quiet_start", s_model.snapshot.quiet_start);
    (void)nvs_set_u16(s_preferences, "quiet_end", s_model.snapshot.quiet_end);
    (void)nvs_set_u8(s_preferences, "brightness", s_brightness);
    (void)nvs_set_u16(s_preferences, "timeout", s_screen_timeout);
    (void)nvs_set_blob(s_preferences, "pending", &s_model.pending, sizeof(s_model.pending));
    esp_err_t result = nvs_commit(s_preferences);
    if (result != ESP_OK) ESP_LOGW(TAG, "Settings save failed: %s", esp_err_to_name(result));
}

static void load_preferences(void)
{
    if (nvs_open("am_monitor", NVS_READWRITE, &s_preferences) != ESP_OK) return;
    s_preferences_ready = true;
    uint8_t muted, brightness;
    uint16_t quiet_start, quiet_end, timeout;
    if (nvs_get_u8(s_preferences, "muted", &muted) == ESP_OK) s_model.snapshot.muted = muted != 0;
    if (nvs_get_u16(s_preferences, "quiet_start", &quiet_start) == ESP_OK && quiet_start < 1440) s_model.snapshot.quiet_start = quiet_start;
    if (nvs_get_u16(s_preferences, "quiet_end", &quiet_end) == ESP_OK && quiet_end < 1440) s_model.snapshot.quiet_end = quiet_end;
    if (nvs_get_u8(s_preferences, "brightness", &brightness) == ESP_OK && brightness > 0 && brightness <= 100) s_brightness = brightness;
    if (nvs_get_u16(s_preferences, "timeout", &timeout) == ESP_OK) s_screen_timeout = timeout;
    size_t size = sizeof(s_model.pending);
    if (nvs_get_blob(s_preferences, "pending", &s_model.pending, &size) != ESP_OK
        || size != sizeof(s_model.pending) || s_model.pending.ack_count > AM_ALERT_MAX) {
        memset(&s_model.pending, 0, sizeof(s_model.pending));
    }
}

static void receive_line(am_transport_t source, const char *line, size_t length)
{
    if (!s_messages || length == 0 || line[0] != '{') return;
    char *copy = malloc(length + 1);
    if (!copy) return;
    memcpy(copy, line, length);
    copy[length] = '\0';
    app_message_t event = {.kind = MESSAGE_LINE, .transport = source, .line = copy, .length = length};
    if (xQueueSend(s_messages, &event, 0) != pdTRUE) free(copy);
}

static void receive_pair(uint32_t passkey)
{
    app_message_t event = {.kind = MESSAGE_PAIR, .passkey = passkey};
    if (s_messages) (void)xQueueSend(s_messages, &event, 0);
}

static void receive_key(bsp_btn_t button, bsp_btn_ev_t event, void *context)
{
    (void)context;
    app_message_t input = {.kind = MESSAGE_KEY, .key = button, .key_event = event};
    if (s_messages) (void)xQueueSend(s_messages, &input, 0);
}

static void wake_display(int64_t now_ms)
{
    s_last_interaction = now_ms;
    if (s_asleep) {
        bsp_display_backlight(s_brightness);
        s_asleep = false;
    }
}

static void audio_task(void *context)
{
    (void)context;
    int kind;
    int16_t samples[256];
    for (;;) {
        if (xQueueReceive(s_audio, &kind, portMAX_DELAY) != pdTRUE || !s_audio_ready) continue;
        if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) continue;
        bsp_audio_set_volume(60);
        const int frequencies[3][2] = {{1400, 1900}, {1300, 900}, {1700, 1700}};
        for (int tone = 0; tone < 2; tone++) {
            int count = 1280;
            for (int position = 0; position < count; position += 256) {
                for (int i = 0; i < 256; i++) {
                    int index = position + i;
                    float envelope = (1.0f - (float)index / count) * fminf(1.0f, index / 100.0f);
                    samples[i] = (int16_t)(15000.0f * envelope
                        * sinf(6.2831853f * frequencies[kind][tone] * index / 16000.0f));
                }
                if (bsp_audio_write(samples, sizeof(samples)) != ESP_OK) break;
            }
            memset(samples, 0, sizeof(samples));
            (void)bsp_audio_write(samples, sizeof(samples));
        }
        memset(samples, 0, sizeof(samples));
        (void)bsp_audio_write(samples, sizeof(samples));
        (void)bsp_audio_sleep();
    }
}

static void handle_key(const app_message_t *event, int64_t now_ms)
{
    if (event->key_event == BSP_BTN_PRESS) {
        bool was_asleep = s_asleep;
        wake_display(now_ms);
        if (was_asleep) s_suppress_keys_until = now_ms + 700;
        return;
    }
    if (now_ms < s_suppress_keys_until) return;
    if (event->key_event != BSP_BTN_CLICK && event->key_event != BSP_BTN_LONG) return;
    wake_display(now_ms);
    if (s_passkey) return;
    if (s_notice_until > now_ms) {
        if (event->key == BSP_BTN_OK && event->key_event == BSP_BTN_CLICK) {
            for (size_t i = 0; i < s_model.snapshot.alert_count; i++) {
                if (strcmp(s_model.snapshot.alerts[i].id, s_notice.id) != 0) continue;
                char id[AM_ID_MAX];
                if (am_model_ack(&s_model, i, id, sizeof(id))) {
                    save_preferences();
                    send_pending();
                }
                break;
            }
        }
        s_notice_until = 0;
        return;
    }
    if (event->key_event == BSP_BTN_LONG) {
        if (event->key == BSP_BTN_OK) (void)am_model_navigate(&s_model, AM_NAV_BACK);
        return;
    }
    am_navigation_t navigation = event->key == BSP_BTN_OK ? AM_NAV_OK
        : event->key == BSP_BTN_UP ? AM_NAV_UP : AM_NAV_DOWN;
    am_action_t action = am_model_navigate(&s_model, navigation);
    if (action == AM_ACTION_ACK) {
        char id[AM_ID_MAX];
        if (am_model_ack(&s_model, s_model.selected % s_model.snapshot.alert_count, id, sizeof(id))) {
            save_preferences();
            send_pending();
        }
    } else if (action != AM_ACTION_NONE) {
        switch (action) {
        case AM_ACTION_MUTE:
            am_model_set_mute(&s_model, !s_model.snapshot.muted);
            send_pending();
            break;
        case AM_ACTION_BRIGHTNESS:
            s_brightness = s_brightness >= 100 ? 25 : s_brightness >= 70 ? 100 : 70;
            bsp_display_backlight(s_brightness);
            break;
        case AM_ACTION_TIMEOUT: s_screen_timeout = s_screen_timeout == 20 ? 60 : s_screen_timeout == 60 ? 0 : 20; break;
        default: break;
        }
        save_preferences();
    }
}

static void handle_line(const app_message_t *event, int64_t now_ms)
{
    am_effect_t effect;
    if (!am_protocol_apply(&s_model, event->line, event->length, event->transport, now_ms, &effect)) return;
    if (effect.hello) send_hello(event->transport);
    if (effect.received) {
        cJSON *object = message("received");
        if (object) cJSON_AddNumberToObject(object, "seq", (double)s_model.snapshot.sequence);
        send_json(event->transport, object);
        send_pending();
        s_passkey = 0;
        size_t count = am_model_selection_count(&s_model);
        if (count > 0) s_model.selected %= count;
        else s_model.selected = 0;
    }
    if (effect.settings_changed || effect.pending_changed) save_preferences();
    if (effect.wake) wake_display(now_ms);
    if (effect.alert_id[0]) {
        for (size_t i = 0; i < s_model.snapshot.alert_count; i++) {
            if (strcmp(s_model.snapshot.alerts[i].id, effect.alert_id) == 0) {
                s_notice = s_model.snapshot.alerts[i];
                s_notice_until = now_ms + 6000;
                break;
            }
        }
    }
    if (effect.sound) {
        int kind = strcmp(effect.kind, "failed") == 0 ? 1 : strcmp(effect.kind, "needs_action") == 0 ? 2 : 0;
        (void)xQueueSend(s_audio, &kind, 0);
    }
}

static void app_task(void *context)
{
    (void)context;
    int64_t last_render = -1000;
    int64_t last_battery = -30000;
    int battery = -1;
    bool dirty = true;
    for (;;) {
        app_message_t event;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (xQueueReceive(s_messages, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (event.kind == MESSAGE_LINE) {
                handle_line(&event, now_ms);
                free(event.line);
            } else if (event.kind == MESSAGE_KEY) {
                handle_key(&event, now_ms);
            } else if (event.kind == MESSAGE_PAIR) {
                s_passkey = event.passkey;
                s_passkey_until = now_ms + 60000;
                wake_display(now_ms);
            }
            dirty = true;
        }
        if (s_passkey && now_ms >= s_passkey_until) {
            s_passkey = 0;
            dirty = true;
        }
        if (now_ms - last_battery >= 30000) {
            battery = bsp_battery_soc();
            last_battery = now_ms;
            ESP_LOGI(TAG, "heap=%u largest=%u usb_age_ms=%" PRId64 " ble=%d",
                (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                s_model.has_snapshot ? now_ms - s_model.received_ms : -1, am_transport_ble_connected());
        }
        if (!s_asleep && !s_passkey && s_screen_timeout > 0
            && now_ms - s_last_interaction >= (int64_t)s_screen_timeout * 1000) {
            bsp_display_backlight(0);
            s_asleep = true;
        }
        if (!s_asleep && (dirty || now_ms - last_render >= 1000) && bsp_lvgl_lock(200)) {
            am_view_render(&s_model, now_ms, battery, s_brightness, s_screen_timeout, s_passkey,
                s_notice_until > now_ms ? &s_notice : NULL);
            bsp_lvgl_unlock();
            last_render = now_ms;
            dirty = false;
        }
    }
}

void app_main(void)
{
    am_model_init(&s_model);
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_OK) load_preferences();
    else ESP_LOGE(TAG, "NVS unavailable: %s; stored data was not erased", esp_err_to_name(nvs_result));
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display LVGL initialization failed");
        return;
    }
    bsp_display_backlight(s_brightness);
    (void)bsp_battery_init();
    s_audio_ready = bsp_audio_init() == ESP_OK;
    s_messages = xQueueCreate(12, sizeof(app_message_t));
    s_audio = xQueueCreate(4, sizeof(int));
    if (!s_messages || !s_audio) {
        ESP_LOGE(TAG, "Application queues unavailable");
        return;
    }
    s_last_interaction = esp_timer_get_time() / 1000;
    if (xTaskCreate(audio_task, "am_audio", 4096, NULL, 5, NULL) != pdPASS
        || xTaskCreate(app_task, "am_app", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Application task unavailable");
        return;
    }
    esp_err_t result = bsp_button_init(receive_key, NULL);
    if (result != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable: %s", esp_err_to_name(result));
    result = am_transport_init(receive_line, receive_pair);
    if (result != ESP_OK) ESP_LOGE(TAG, "Transport unavailable: %s", esp_err_to_name(result));
    ESP_LOGI(TAG, "Agent Monitor %s ready; three-button read-only terminal", esp_app_get_description()->version);
}
