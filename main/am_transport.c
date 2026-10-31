#include "am_transport.h"

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "am_transport";
// ESP-IDF's NimBLE peripheral example declares this store initializer explicitly.
void ble_store_config_init(void);
static am_transport_line_cb_t s_line_callback;
static am_transport_pair_cb_t s_pair_callback;
static am_framer_t s_usb_framer;
static am_framer_t s_ble_framer;
static SemaphoreHandle_t s_usb_tx;
static SemaphoreHandle_t s_ble_tx;
static uint16_t s_connection = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle;
static bool s_subscribed;
static bool s_encrypted;
static uint8_t s_addr_type;
static int64_t s_usb_rx_ms;
static char s_name[32] = "AgentPassport";

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);

static void line_received(const char *line, size_t length, void *context)
{
    am_transport_t source = (am_transport_t)(uintptr_t)context;
    if (s_line_callback) s_line_callback(source, line, length);
}

void am_transport_send(am_transport_t destination, const char *line)
{
    size_t length = strlen(line);
    if (destination == AM_USB) {
        if (!s_usb_tx || xSemaphoreTake(s_usb_tx, pdMS_TO_TICKS(100)) != pdTRUE) return;
        (void)usb_serial_jtag_write_bytes(line, length, pdMS_TO_TICKS(50));
        (void)usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(20));
        xSemaphoreGive(s_usb_tx);
        return;
    }
    if (!s_ble_tx || xSemaphoreTake(s_ble_tx, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (s_connection != BLE_HS_CONN_HANDLE_NONE && s_subscribed && s_encrypted) {
        uint16_t connection = s_connection;
        uint16_t mtu = ble_att_mtu(connection);
        size_t capacity = mtu > 3 ? mtu - 3 : 20;
        for (size_t offset = 0; offset < length;) {
            size_t size = length - offset < capacity ? length - offset : capacity;
            struct os_mbuf *packet = ble_hs_mbuf_from_flat(line + offset, size);
            if (!packet || ble_gattc_notify_custom(connection, s_tx_handle, packet) != 0) break;
            offset += size;
            vTaskDelay(pdMS_TO_TICKS(3));
        }
        struct os_mbuf *end = ble_hs_mbuf_from_flat("\n", 1);
        if (end) (void)ble_gattc_notify_custom(connection, s_tx_handle, end);
    }
    xSemaphoreGive(s_ble_tx);
}

static void hello(am_transport_t source)
{
    char line[160];
    snprintf(line, sizeof(line),
        "{\"v\":1,\"type\":\"hello\",\"device\":\"ai-passport\",\"firmware\":\"%s\",\"transport\":\"%s\"}",
        esp_app_get_description()->version, source == AM_USB ? "usb" : "ble");
    am_transport_send(source, line);
}

static void usb_task(void *arg)
{
    (void)arg;
    uint8_t bytes[256];
    int64_t last_hello = -5000;
    for (;;) {
        int received = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(30));
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (received > 0) {
            s_usb_rx_ms = now_ms;
            am_framer_feed(&s_usb_framer, bytes, (size_t)received, line_received, (void *)(uintptr_t)AM_USB);
        }
        if (now_ms - s_usb_rx_ms > 5000 && now_ms - last_hello > 5000) {
            hello(AM_USB);
            last_hello = now_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static int uart_access(uint16_t connection, uint16_t attribute,
                       struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)connection;
    (void)attribute;
    (void)arg;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    uint8_t bytes[256];
    uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length > sizeof(bytes)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), &length) != 0) return BLE_ATT_ERR_UNLIKELY;
    am_framer_feed(&s_ble_framer, bytes, length, line_received, (void *)(uintptr_t)AM_BLE);
    return 0;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &s_rx_uuid.u, .access_cb = uart_access,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC },
            { .uuid = &s_tx_uuid.u, .access_cb = uart_access,
              .val_handle = &s_tx_handle, .flags = BLE_GATT_CHR_F_NOTIFY },
            {0}
        },
    },
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)s_name;
    response.name_len = strlen(s_name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;
    struct ble_gap_adv_params parameters = {0};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &parameters, gap_event, NULL);
}

static void ble_hello_task(void *arg)
{
    (void)arg;
    hello(AM_BLE);
    vTaskDelete(NULL);
}

static void schedule_hello(void)
{
    if (!s_subscribed || !s_encrypted) return;
    if (xTaskCreate(ble_hello_task, "am_ble_hello", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "BLE hello task unavailable");
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connection = event->connect.conn_handle;
            s_subscribed = false;
            s_encrypted = false;
            memset(&s_ble_framer, 0, sizeof(s_ble_framer));
            (void)ble_gap_security_initiate(s_connection);
        } else {
            (void)advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connection = BLE_HS_CONN_HANDLE_NONE;
        s_encrypted = false;
        s_subscribed = false;
        memset(&s_ble_framer, 0, sizeof(s_ble_framer));
        (void)advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = event->subscribe.cur_notify;
            schedule_hello();
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc connection;
        if (event->enc_change.status == 0 && ble_gap_conn_find(event->enc_change.conn_handle, &connection) == 0) {
            s_encrypted = connection.sec_state.encrypted;
            schedule_hello();
        }
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io key = {0};
        key.action = event->passkey.params.action;
        if (key.action == BLE_SM_IOACT_DISP) {
            key.passkey = 100000 + esp_random() % 900000;
            if (s_pair_callback) s_pair_callback(key.passkey);
            (void)ble_sm_inject_io(event->passkey.conn_handle, &key);
        }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        (void)advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) {
        uint8_t address[6];
        if (ble_hs_id_copy_addr(s_addr_type, address, NULL) == 0) {
            snprintf(s_name, sizeof(s_name), "AgentPassport-%02X%02X", address[1], address[0]);
            (void)ble_svc_gap_device_name_set(s_name);
        }
        rc = advertise();
    }
    if (rc != 0) ESP_LOGE(TAG, "BLE advertising failed: %d", rc);
}

static void on_reset(int reason)
{
    s_connection = BLE_HS_CONN_HANDLE_NONE;
    s_subscribed = false;
    s_encrypted = false;
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    for (;;) vTaskSuspend(NULL);
}

esp_err_t am_transport_init(am_transport_line_cb_t line_callback,
                            am_transport_pair_cb_t pair_callback)
{
    s_line_callback = line_callback;
    s_pair_callback = pair_callback;
    s_usb_tx = xSemaphoreCreateMutex();
    s_ble_tx = xSemaphoreCreateMutex();
    if (!s_usb_tx || !s_ble_tx) return ESP_ERR_NO_MEM;
    usb_serial_jtag_driver_config_t configuration = {.tx_buffer_size = 1024, .rx_buffer_size = 4096};
    esp_err_t result = usb_serial_jtag_driver_install(&configuration);
    if (result != ESP_OK) return result;
    if (xTaskCreate(usb_task, "am_usb", 3072, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    result = nimble_port_init();
    if (result != ESP_OK) return result;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    int rc = ble_gatts_count_cfg(s_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) return ESP_FAIL;
    (void)ble_att_set_preferred_mtu(247);
    if (xTaskCreatePinnedToCore(host_task, "am_nimble", NIMBLE_HS_STACK_SIZE, NULL,
                               configMAX_PRIORITIES - 4, NULL, NIMBLE_CORE) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool am_transport_ble_connected(void)
{
    return s_connection != BLE_HS_CONN_HANDLE_NONE && s_encrypted;
}

const char *am_transport_ble_name(void)
{
    return s_name;
}
