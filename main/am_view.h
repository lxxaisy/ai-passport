#pragma once

#include "am_model.h"

void am_view_render(const am_model_t *model, int64_t now_ms, int battery,
                     unsigned brightness, unsigned screen_timeout, uint32_t pairing_code,
                     const am_alert_t *notice);
