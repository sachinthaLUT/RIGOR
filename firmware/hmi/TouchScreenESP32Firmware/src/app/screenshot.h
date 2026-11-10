#pragma once
#include <stddef.h>

// ===========================================================================
//  SCREENSHOT  --  pixel-exact 800x480 captures of the UI, saved to the SD
//  card as 24-bit BMP files in /shots (scr_00.bmp, scr_01.bmp, ...).
//
//  What is captured is exactly what LVGL renders and sends to the panel:
//  the active screen plus lv_layer_top() (modals, keypads, toasts), blended.
//  The FPS/CPU monitor lives on the system layer and is never included.
//
//  HOW TO USE
//    1. lv_conf.h:  #define LV_USE_SNAPSHOT 1      (required)
//                   #define LV_USE_PERF_MONITOR 0  (so the panel looks clean too)
//    2. In the .ino: call screenshot_poll() from loop(), and
//       screenshot_arm_splash() inside the lock right after ui_init().
//    3. Insert a FAT32 SD card, open the Serial Monitor on the display's USB
//       port (115200), navigate to a screen and type  p  then Enter.
//       The splash is captured automatically at boot (it is gone after 1.8 s).
//    4. Take the card out and read /shots on a PC.
//
//  Needs ~2 MB of free PSRAM for one capture (two 800x480 snapshots).
// ===========================================================================

// Capture now. Call ONLY with the LVGL lock already held (e.g. from an
// lv_timer callback). Writes the file name into out (may be nullptr).
bool screenshot_take_locked(char* out, size_t outlen);

// Capture now, taking the LVGL lock itself. Call from loop() / other tasks.
bool screenshot_take(char* out, size_t outlen);

// Poll the USB Serial console for the 'p' command. Call from loop().
void screenshot_poll();

// One-shot timer that captures the splash screen shortly after boot.
// Call once, inside the LVGL lock, right after ui_init().
void screenshot_arm_splash(unsigned delay_ms = 900);
