#pragma once

#include "am_model.h"
#include "esp_err.h"

typedef void (*am_transport_line_cb_t)(am_transport_t source, const char *line, size_t length);
typedef void (*am_transport_pair_cb_t)(uint32_t passkey);

esp_err_t am_transport_init(am_transport_line_cb_t line_callback,
                            am_transport_pair_cb_t pair_callback);
void am_transport_send(am_transport_t destination, const char *line);
bool am_transport_ble_connected(void);
const char *am_transport_ble_name(void);
