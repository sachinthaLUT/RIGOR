#pragma once
#include <stdint.h>

// ===========================================================================
//  HAL  --  the few places the UI touches hardware directly.
//
//  WHY THIS EXISTS: your esp_panel_board_custom_conf.h sets
//      ESP_PANEL_BOARD_USE_BACKLIGHT  = 1
//      ESP_PANEL_BOARD_BACKLIGHT_TYPE = PWM_LEDC
//      ESP_PANEL_BOARD_BACKLIGHT_IO   = 2
//  so ESP32_Display_Panel ALREADY owns an LEDC channel on GPIO2. The UI must
//  not call ledcAttach(2, ...) itself -- two owners on one pin fight.
//
//  The default below is WEAK and does nothing. To make the brightness slider
//  work, define this in TouchScreenESP32Firmware.ino (where the Board lives):
//
//      #include "src/app/hal.h"
//      void hal_set_backlight(uint8_t pct) {
//          if (!board) return;
//          auto bl = board->getBacklight();
//          if (bl) bl->setBrightness(pct);   // 0..100
//      }
//
//  A non-weak definition anywhere in the sketch overrides the stub at link
//  time -- no #ifdef needed.
// ===========================================================================
void hal_set_backlight(uint8_t percent);
