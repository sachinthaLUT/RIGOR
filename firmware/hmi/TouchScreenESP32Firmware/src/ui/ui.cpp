#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "estop_overlay.h"
#include "../app/state.h"
#include "../app/hal.h"
#include "../link/link.h"
#include "../app/webserver.h"
#include <string.h>
#include <stdio.h>

typedef lv_obj_t* (*screen_factory_t)();
typedef void      (*screen_refresh_t)();

struct ScreenDef {
    screen_factory_t create;
    screen_refresh_t refresh;   // may be nullptr
};

// One row per ScreenId, SAME ORDER as the enum in ui.h.
static const ScreenDef k_defs[SCREEN_COUNT] = {
    { screen_splash_create,      nullptr                  },  // SPLASH
    { screen_home_create,        screen_home_refresh      },  // HOME
    { screen_static_create,      screen_static_refresh    },  // STATIC
	{ screen_stepped_create,  	 screen_stepped_refresh },  // STEPPED
	{ screen_dynamic_create,     screen_dynamic_refresh },  // DYNAMIC
    { screen_calibration_create, screen_calibration_refresh },// CALIB
    { screen_diagnosis_create,   screen_diagnosis_refresh },  // DIAG
    { screen_settings_create,    screen_settings_refresh  },  // SETTINGS
    { screen_about_create,       nullptr                  },  // ABOUT
};

static lv_obj_t* s_cache[SCREEN_COUNT] = { nullptr };
static ScreenId  s_current             = SCREEN_SPLASH;

ScreenId ui_current() { return s_current; }

// ===========================================================================
//  GLOBAL TICK
//
//  Runs regardless of which screen is showing:
//    1. app-wide reactions (e-stop overlay, fault popup, link-loss toast)
//    2. the active screen's refresh hook
//
//  Keeping these HERE rather than in each screen means an overload or an
//  e-stop raises the alarm even if the operator is sitting on the About page.
// ===========================================================================
static bool     s_last_link_ok  = false;
static bool     s_fault_shown   = false;

static void fault_reset_confirmed() {
    link_cmd_reset();
    w_toast("Reset sent - rig will back off", COL_BLUE, 2500);
}

// Is the e-stop overlay handling this fault? es=1 and st=FAULT arrive in the
// same telemetry frame, so checking the flag is enough. (A rig found already
// faulted with "E-STOP" and the switch released -- e.g. the display rebooted --
// gets the normal popup, which offers the same reset.)
static bool fault_is_estop() {
    return g_app.estop || estop_overlay_is_open();
}

static void handle_global_events() {
    // ---- Emergency stop ----------------------------------------------------
    // Runs FIRST so that on the frame where the rig reports es=1 together
    // with st=FAULT, the overlay wins and the generic popup is suppressed.
    estop_overlay_update();

    // ---- Rig entered a latched fault ------------------------------------
    if (g_app.rig == RigState::Fault && !s_fault_shown) {
        s_fault_shown = true;

        if (!fault_is_estop()) {
            char msg[180];
            snprintf(msg, sizeof(msg),
                     "%s\n\nThe motor is latched off. Clear the cause, then reset.",
                     g_app.fault[0] ? g_app.fault : "The rig reported a fault.");

            link_cmd_beep();   // no-op when the buzzer is Off

            w_modal("RIG FAULT", msg, COL_RED,
                    "Reset rig", "Dismiss", fault_reset_confirmed);
        }
    }

    // Re-arm once the rig leaves the fault state, so the NEXT fault pops up.
    if (g_app.rig != RigState::Fault) s_fault_shown = false;

    // ---- Link dropped / recovered ---------------------------------------
    if (s_last_link_ok && !g_app.link_ok) {
        w_toast(LV_SYMBOL_WARNING "  Lost link to rig", COL_ORANGE, 3000);
    } else if (!s_last_link_ok && g_app.link_ok) {
        w_toast(LV_SYMBOL_OK "  Rig connected", COL_GREEN, 1800);
    }

    s_last_link_ok = g_app.link_ok;
}

static void ui_tick_cb(lv_timer_t* /*t*/) {
    web_poll();

    handle_global_events();

    ui_topbar_refresh(lv_scr_act());

    screen_refresh_t r = k_defs[s_current].refresh;
    if (r) r();
}

// ===========================================================================
//  NAVIGATION
// ===========================================================================
void ui_goto(ScreenId id) {
    if (id < 0 || id >= SCREEN_COUNT) return;

    // Enter / leave hooks. Only Calibration needs one: it raises the rig's
    // telemetry rate while it is open and puts it back when you leave.
    if (s_current == SCREEN_CALIB && id != SCREEN_CALIB) screen_calibration_leave();

    if (!s_cache[id]) s_cache[id] = k_defs[id].create();
    const ScreenId prev = s_current;
    s_current = id;
    if (id == SCREEN_CALIB && prev != SCREEN_CALIB) screen_calibration_enter();

    lv_scr_load_anim(s_cache[id], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    screen_refresh_t r = k_defs[id].refresh;
    if (r) r();
    ui_topbar_refresh(s_cache[id]);
}

void ui_init() {
    state_init();
    hal_set_backlight(100);     // fixed: brightness is no longer a setting

    lv_timer_create(ui_tick_cb, 50, nullptr);  // 20 Hz UI refresh
    ui_goto(SCREEN_SPLASH);
}
