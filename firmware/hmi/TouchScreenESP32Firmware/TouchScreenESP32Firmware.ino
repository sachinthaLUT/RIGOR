/* =====================================================================
 *  RIGOR  --  HMI (touchscreen) firmware v1.3.1
 *  Runs on the display ESP32-S3. Talks to the actuator ESP32 over Serial1.
 *
 *  This file is a REFERENCE for the two things the UI needs from you:
 *    1. ui_init() and link_begin() called once, inside the LVGL lock
 *    2. the hal_set_backlight() override (backlight fixed at 100 %)
 *
 *  Keep your existing panel/LVGL bring-up if it already works - only merge
 *  in the marked sections.
 *
 *  SCREENSHOTS (for the paper): see src/app/screenshot.h. Type  p  + Enter in
 *  the Serial Monitor to save the current screen to /shots on the SD card.
 *  Set ENABLE_SCREENSHOTS to 0 for normal use.
 * ===================================================================== */

#include <esp_display_panel.hpp>
#include <esp_log.h>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#include "src/ui/ui.h"
#include "src/link/link.h"
#include "src/app/hal.h"
#include "src/app/datalog.h"

#define ENABLE_SCREENSHOTS 0      // 1 = 'p' on the USB console saves a BMP
#if ENABLE_SCREENSHOTS
#include "src/app/screenshot.h"
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Board* board = nullptr;

// ---------------------------------------------------------------------------
//  BACKLIGHT  --  fixed at 100 %. (The brightness setting was removed 10/2026.)
//
//  hal.cpp defines hal_set_backlight() as a WEAK no-op; this definition
//  overrides it at link time. ui_init() calls it once with 100.
//
//  Do NOT call ledcAttach() on GPIO2 yourself: esp_panel_board_custom_conf.h
//  has ESP_PANEL_BOARD_USE_BACKLIGHT (1) with PWM_LEDC, so the panel driver
//  already owns that pin.
// ---------------------------------------------------------------------------
void hal_set_backlight(uint8_t pct) {
    if (!board) return;
    auto* bl = board->getBacklight();
    if (bl) bl->setBrightness(pct > 100 ? 100 : pct);
}

void setup() {
    Serial.begin(115200);             // USB console (USB CDC, not UART0)

    // -----------------------------------------------------------------
    //  SILENCE UART0  <-- REQUIRED, the link shares these pins
    //
    //  The rig link runs on GPIO43/44, which are also UART0 -- where the
    //  ESP-IDF logger writes by default. Anything logged there arrives at
    //  the rig as junk. This kills it for every IDF component.
    //
    //  ("USB CDC On Boot" moves the Serial OBJECT to USB; it does not move
    //   the IDF logger, and it cannot free the pins.)
    //
    //  Note: the ROM bootloader banner ("ESP-ROM:", "rst:0x1", "load:0x...")
    //  is printed by mask ROM before any of this runs and CANNOT be
    //  suppressed. The rig's command validator rejects those lines, so they
    //  are harmless -- you will just see a short burst of ignored text on the
    //  rig console each time the display reboots.
    // -----------------------------------------------------------------
    esp_log_level_set("*", ESP_LOG_NONE);

    board = new Board();
    board->init();
    board->begin();

    lvgl_port_init(board->getLCD(), board->getTouch());

    lv_disp_set_rotation(lv_disp_get_default(), LV_DISP_ROT_180);

    // Everything that touches LVGL objects or creates lv_timers must happen
    // inside the lock, because the LVGL task is already running by now.
    lvgl_port_lock(-1);

    datalog_begin(); // mount the SD card (safe with no card inserted)
    ui_init();       // loads settings from NVS, builds splash, starts UI tick
    link_begin();    // opens Serial1 + starts the 50 Hz RX pump timer
#if ENABLE_SCREENSHOTS
    screenshot_arm_splash(900);   // the splash is only visible for ~1.8 s
#endif

    lvgl_port_unlock();
}

void loop() {
    // LVGL runs on its own task (lvgl_v8_port), the link is pumped by an
    // lv_timer, and the UI refreshes on another lv_timer. The loop only
    // watches the USB console for the screenshot command.
#if ENABLE_SCREENSHOTS
    screenshot_poll();
    delay(50);
#else
    delay(1000);
#endif
}
