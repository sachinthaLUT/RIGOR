#pragma once
#include <lvgl.h>

// ===========================================================================
//  UI  --  screen registry and navigation.
//
//  ADDING A SCREEN:
//    1. add an entry to the ScreenId enum below (before SCREEN_COUNT)
//    2. declare its _create() (and optional _refresh()) function here
//    3. add a row to k_defs[] in ui.cpp, IN THE SAME ORDER as the enum
//    4. create the .cpp in src/ui/
// ===========================================================================

// ---------------------------------------------------------------------------
//  IDENTITY  --  shown on the splash, About, Settings and the Wi-Fi page.
//  Bump HMI_FW_VERSION whenever you flash a new build.
// ---------------------------------------------------------------------------
#define APP_NAME        "RIGOR"
#define APP_AUTHOR      "Sachintha Weerasinghe"
#define APP_DATE        "10 / 2026"
#define HMI_FW_VERSION  "v1.3.1"

enum ScreenId {
    SCREEN_SPLASH = 0,
    SCREEN_HOME,
    SCREEN_STATIC,
    SCREEN_STEPPED,     // "Stepped Loading" (profile-based step sequence)
    SCREEN_DYNAMIC,    // "Dynamic Loading" (square-wave cyclic test)
    SCREEN_CALIB,
    SCREEN_DIAG,
    SCREEN_SETTINGS,
    SCREEN_ABOUT,
    SCREEN_COUNT
};

// Build the UI and show the splash. Call ONCE from setup(), inside the LVGL
// lock (see the .ino).
void ui_init();

// Navigate. Screens are built lazily on first visit, then cached, so going
// back to a screen is instant and its widget pointers stay valid.
void ui_goto(ScreenId id);

// Which screen is showing right now.
ScreenId ui_current();

// ---------------------------------------------------------------------------
//  SCREEN FACTORIES  --  one per file in src/ui/
// ---------------------------------------------------------------------------
lv_obj_t* screen_splash_create();
lv_obj_t* screen_home_create();
lv_obj_t* screen_static_create();
lv_obj_t* screen_stepped_create();
lv_obj_t* screen_dynamic_create();
lv_obj_t* screen_calibration_create();
lv_obj_t* screen_diagnosis_create();
lv_obj_t* screen_settings_create();
lv_obj_t* screen_about_create();

// ---------------------------------------------------------------------------
//  LIVE REFRESH HOOKS
//  Called ~20 Hz for the ACTIVE screen only, on the LVGL task, so they may
//  touch widgets directly. Read g_app, write widgets, nothing else.
// ---------------------------------------------------------------------------
void screen_home_refresh();
void screen_static_refresh();
void screen_stepped_refresh();
void screen_dynamic_refresh();

// Let a test screen tell Home what just finished. RAM-only by design: it
// resets on reboot and costs no flash writes.
void home_set_last_test(const char* txt);
void screen_calibration_refresh();
void screen_diagnosis_refresh();
void screen_settings_refresh();

// Calibration raises the telemetry rate while open (called by ui_goto).
void screen_calibration_enter();
void screen_calibration_leave();
