#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../app/datalog.h"
#include "../app/webserver.h"
#include "../link/link.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  DYNAMIC LOADING  --  square wave (cyclic / fatigue) testing
//
//  Top    : five parameter cards (bottom load, top load, both dwells, cycles)
//  Middle : live force-vs-time chart with the two commanded levels marked
//  Bottom : cycle/elapsed/peak stats plus motor current, driver temperature
//           and peak current; Download, START-PAUSE-CONTINUE, STOP
//
//  ---------------------------------------------------------------------
//  WHY SQUARE ONLY
//  ---------------------------------------------------------------------
//  The rig's force loop is tuned to SETTLE, not to TRACK. A square wave asks
//  it to do the one thing it is good at: go to a force and hold. What comes
//  out is trapezoidal (ramp-dwell-ramp-dwell), a standard fatigue profile.
//
//  ---------------------------------------------------------------------
//  THE CYCLER  --  this board is driving a loaded machine
//  ---------------------------------------------------------------------
//    - link loss for SEQ_LINK_ABORT_MS  -> stop and abort
//    - rig fault (incl. e-stop)         -> stop and abort
//    - hold timer starts ON ARRIVAL at the level
//  The RIG runs its own watchdog too, so if this display dies the rig stops.
//
//  LOGGING: each sample is time, force, motor current and driver
//  temperature (see datalog.h for the CSV columns).
//
//  PAUSE sends S: the motor stops and the leadscrew self-locks, holding
//  position. Force is NOT being regulated while paused, and the UI says so.
// ===========================================================================

static const uint32_t SEQ_TICK_MS       = 100;
static const uint32_t SEQ_LINK_ABORT_MS = 1500;
static const uint32_t ARRIVE_TIMEOUT_MS = 60000;
static const int      TELEM_HZ_RUN      = 50;
static const int      TELEM_HZ_IDLE     = 4;

// ---- Layout ---------------------------------------------------------------
static const int PARAM_Y = TOPBAR_H + 6;
static const int PARAM_H = 76;
static const int PARAM_W = (SCREEN_W - 2 * PAD_EDGE - 4 * 10) / 5;
static const int CHART_Y = PARAM_Y + PARAM_H + 26;
static const int CHART_H = 178;

static const int JOG_W   = 122;
static const int JOG_H   = (CHART_H - 2 * 10) / 3;      // = 52
static const int JOG_X   = SCREEN_W - PAD_EDGE - JOG_W;
static const int CHART_W = SCREEN_W - 2 * PAD_EDGE - JOG_W - 10;

static const int BOT_Y   = CHART_Y + CHART_H + 10;
static const int BOT_H   = SCREEN_H - BOT_Y - PAD_EDGE;

// ---- Widgets --------------------------------------------------------------
static lv_obj_t* s_pv[5]      = { nullptr };
static lv_obj_t* s_info       = nullptr;
static lv_obj_t* s_chart      = nullptr;
static lv_chart_series_t* s_ser = nullptr;
static lv_obj_t* s_live       = nullptr;
static lv_obj_t* s_cycle_lbl  = nullptr;
static lv_obj_t* s_elapsed_lbl= nullptr;
static lv_obj_t* s_peak_lbl   = nullptr;
static lv_obj_t* s_cur_lbl    = nullptr;   // motor current
static lv_obj_t* s_temp_lbl   = nullptr;   // driver temperature
static lv_obj_t* s_pki_lbl    = nullptr;   // peak current this run
static lv_obj_t* s_go_btn     = nullptr;
static lv_obj_t* s_name_lbl   = nullptr;
static lv_obj_t* s_jog_btn[3] = { nullptr };

// ---- Run state ------------------------------------------------------------
enum class DynPhase : uint8_t { Idle, Paused, ToBottom, AtBottom, ToTop, AtTop };
static DynPhase    s_phase      = DynPhase::Idle;
static DynPhase    s_resume     = DynPhase::ToBottom;
static uint32_t    s_phase_ms   = 0;
static uint32_t    s_cycle      = 0;
static uint32_t    s_run_start  = 0;
static uint32_t    s_paused_at  = 0;
static uint32_t    s_paused_tot = 0;
static float       s_peak       = 0.0f;
static float       s_peak_i     = 0.0f;    // largest |current| this run, A
static lv_timer_t* s_timer      = nullptr;

#define CHART_POINTS 120

static void dyn_stop(const char* why, lv_color_t col);

// ===========================================================================
//  PAINTING
// ===========================================================================
static void paint_params() {
    char b[24];
    snprintf(b, sizeof(b), "%.0f", g_dyn.bottom_kg); lv_label_set_text(s_pv[0], b);
    snprintf(b, sizeof(b), "%.0f", g_dyn.top_kg);    lv_label_set_text(s_pv[1], b);
    snprintf(b, sizeof(b), "%u", (unsigned)g_dyn.bottom_s); lv_label_set_text(s_pv[2], b);
    snprintf(b, sizeof(b), "%u", (unsigned)g_dyn.top_s);    lv_label_set_text(s_pv[3], b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)g_dyn.cycles); lv_label_set_text(s_pv[4], b);

    const uint32_t cyc  = dyn_cycle_seconds();
    const uint32_t tot  = cyc * g_dyn.cycles;
    char info[112];
    snprintf(info, sizeof(info),
             "%lu s/cycle (%.3f Hz)  -  est. %luh %02lum",
             (unsigned long)cyc, cyc ? 1.0 / (double)cyc : 0.0,
             (unsigned long)(tot / 3600), (unsigned long)((tot % 3600) / 60));
    lv_label_set_text(s_info, info);

    if (s_name_lbl) lv_label_set_text(s_name_lbl, g_dyn.name);
}

static void rescale_chart() {
    const float pad = 5.0f;
    int lo = (int)(g_dyn.bottom_kg - pad); if (lo < 0) lo = 0;
    int hi = (int)(g_dyn.top_kg + pad);
    if (hi <= lo) hi = lo + 10;
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
}

// ===========================================================================
//  THE CYCLER
// ===========================================================================
static void set_go_button() {
    switch (s_phase) {
        case DynPhase::Idle:
            w_button_set_text(s_go_btn, LV_SYMBOL_PLAY "  START");
            w_button_set_style(s_go_btn, WBTN_SUCCESS);
            break;
        case DynPhase::Paused:
            w_button_set_text(s_go_btn, LV_SYMBOL_PLAY "  CONTINUE");
            w_button_set_style(s_go_btn, WBTN_SUCCESS);
            break;
        default:
            w_button_set_text(s_go_btn, LV_SYMBOL_PAUSE "  PAUSE");
            w_button_set_style(s_go_btn, WBTN_PRIMARY);
            break;
    }
}

static void dyn_stop(const char* why, lv_color_t col) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    s_phase = DynPhase::Idle;

    // ALWAYS stop -- and retract, so a completed or aborted cyclic test never
    // leaves the specimen at load. (link.cpp skips the retract when the rig
    // is faulted or e-stopped; it cannot move then anyway.)
    link_cmd_stop_and_retract();
    link_cmd_telemetry_hz(TELEM_HZ_IDLE);
    datalog_stop();

    link_cmd_beep();   // no-op when the buzzer is Off

    lv_label_set_text(s_live, why);
    lv_obj_set_style_text_color(s_live, col, 0);
    set_go_button();
}

static void dyn_tick(lv_timer_t* /*t*/) {
    if (s_phase == DynPhase::Idle || s_phase == DynPhase::Paused) return;

    // ---- Abort conditions first ------------------------------------------
    if (!g_app.link_ok || link_age_ms() > SEQ_LINK_ABORT_MS) {
        dyn_stop("Aborted: link lost", COL_RED);
        w_modal("Test aborted",
                "The link to the rig dropped mid-test.\n\n"
                "A stop was sent, but confirm the rig is unloaded.",
                COL_RED, "OK", nullptr, nullptr);
        return;
    }
    if (g_app.estop) {
        dyn_stop("Aborted: E-STOP", COL_RED);
        return;      // the e-stop overlay explains the rest
    }
    if (g_app.rig == RigState::Fault) {
        dyn_stop("Aborted: rig fault", COL_RED);
        return;      // the global fault modal explains why
    }

    const uint32_t now = millis();
    const uint32_t el  = now - s_phase_ms;

    // ---- Log + chart ------------------------------------------------------
    datalog_add(now - s_run_start - s_paused_tot, g_app.force_kg,
                g_app.current_a, g_app.drv_temp_c,
                g_app.has_drive_data && g_app.drv_temp_ok);
    if (g_app.force_kg > s_peak) s_peak = g_app.force_kg;
    if (fabsf(g_app.current_a) > s_peak_i) s_peak_i = fabsf(g_app.current_a);
    lv_chart_set_next_value(s_chart, s_ser, (lv_coord_t)(g_app.force_kg + 0.5f));

    switch (s_phase) {
        case DynPhase::ToBottom:
            if (fabsf(g_app.force_kg - g_dyn.bottom_kg) <= RIG_DEADBAND_KG) {
                s_phase = DynPhase::AtBottom;
                s_phase_ms = now;
            } else if (el > ARRIVE_TIMEOUT_MS) {
                dyn_stop("Aborted: never reached bottom load", COL_RED);
            }
            break;

        case DynPhase::AtBottom:
            if (el >= (uint32_t)g_dyn.bottom_s * 1000UL) {
                if (s_cycle >= g_dyn.cycles) {
                    dyn_stop("Test complete", COL_GREEN);
                    w_toast("Test complete", COL_GREEN, 3000);
                    return;
                }
                link_cmd_hold(g_dyn.top_kg);    // rig is running: always sent
                s_phase = DynPhase::ToTop;
                s_phase_ms = now;
            }
            break;

        case DynPhase::ToTop:
            if (fabsf(g_app.force_kg - g_dyn.top_kg) <= RIG_DEADBAND_KG) {
                s_phase = DynPhase::AtTop;
                s_phase_ms = now;
            } else if (el > ARRIVE_TIMEOUT_MS) {
                dyn_stop("Aborted: never reached top load", COL_RED);
            }
            break;

        case DynPhase::AtTop:
            if (el >= (uint32_t)g_dyn.top_s * 1000UL) {
                link_cmd_hold(g_dyn.bottom_kg);
                s_phase = DynPhase::ToBottom;
                s_phase_ms = now;
                s_cycle++;
            }
            break;

        default: break;
    }

    // ---- Live line --------------------------------------------------------
    char b[72];
    const char* what =
        (s_phase == DynPhase::ToTop)    ? "ramping to top"    :
        (s_phase == DynPhase::AtTop)    ? "holding top"       :
        (s_phase == DynPhase::ToBottom) ? "ramping to bottom" : "holding bottom";
    snprintf(b, sizeof(b), "%.1f kg  -  %s", g_app.force_kg, what);
    lv_label_set_text(s_live, b);
    lv_obj_set_style_text_color(s_live, COL_GREEN, 0);
}

static void dyn_start() {
    // Command the first level BEFORE opening the log: if the hold is refused
    // (e-stop, driver too hot) there is no test, so no empty file either.
    if (!link_cmd_hold(g_dyn.bottom_kg)) return;

    if (!datalog_start(g_dyn.name)) {
        w_toast("Logging unavailable - running anyway", COL_ORANGE, 2500);
    }
    link_cmd_telemetry_hz(TELEM_HZ_RUN);

    s_phase      = DynPhase::ToBottom;
    s_phase_ms   = millis();
    s_run_start  = millis();
    s_paused_tot = 0;
    s_cycle      = 0;
    s_peak       = 0.0f;
    s_peak_i     = 0.0f;

    lv_chart_set_all_value(s_chart, s_ser, LV_CHART_POINT_NONE);
    rescale_chart();

    if (!s_timer) s_timer = lv_timer_create(dyn_tick, SEQ_TICK_MS, nullptr);
    set_go_button();
}

// ===========================================================================
//  CALLBACKS
// ===========================================================================
static bool editable() {
    if (s_phase != DynPhase::Idle) {
        w_toast("Stop the test before editing", COL_ORANGE, 2000);
        return false;
    }
    return true;
}

static void set_bottom(float v) { g_dyn.bottom_kg = v; dyn_save(); paint_params(); }
static void set_top(float v)    { g_dyn.top_kg    = v; dyn_save(); paint_params(); }
static void set_bdwell(float v) { g_dyn.bottom_s  = (uint16_t)v; dyn_save(); paint_params(); }
static void set_tdwell(float v) { g_dyn.top_s     = (uint16_t)v; dyn_save(); paint_params(); }
static void set_cycles(float v) { g_dyn.cycles    = (uint32_t)v; dyn_save(); paint_params(); }

static void param_cb(lv_event_t* e) {
    if (!editable()) return;
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
        case 0: w_keypad("Bottom Load", "kg", g_dyn.bottom_kg, 0, RIG_MAX_KG, 0, set_bottom); break;
        case 1: w_keypad("Top Load",    "kg", g_dyn.top_kg,    1, RIG_MAX_KG, 0, set_top);    break;
        case 2: w_keypad("Bottom Dwell","s",  g_dyn.bottom_s,  1, 3600, 0, set_bdwell); break;
        case 3: w_keypad("Top Dwell",   "s",  g_dyn.top_s,     1, 3600, 0, set_tdwell); break;
        case 4: w_keypad("Cycles",      "",   g_dyn.cycles,    1, 999999, 0, set_cycles); break;
    }
}

static void name_entered(const char* txt) {
    snprintf(g_dyn.name, sizeof(g_dyn.name), "%s", txt);
    dyn_save();
    paint_params();
}

static void name_cb(lv_event_t* /*e*/) {
    if (!editable()) return;
    w_textkb("Test name (becomes the CSV filename)", g_dyn.name, 32, name_entered);
}

static void start_confirmed() { dyn_start(); }

static void go_cb(lv_event_t* /*e*/) {
    if (s_phase == DynPhase::Idle) {
        if (!g_app.link_ok) { w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000); return; }
        if (g_app.estop) {
            w_toast(LV_SYMBOL_WARNING "  E-stop pressed - release it first", COL_RED, 2500);
            return;
        }
        if (g_app.rig == RigState::Fault) {
            w_modal("Rig is faulted", "Clear the latched fault before starting.",
                    COL_RED, "Reset rig", "Cancel", link_cmd_reset);
            return;
        }
        if (g_app.has_drive_data && g_app.drv_temp_ok &&
            g_app.drv_temp_c >= RIG_TEMP_WARN_C) {
            char t[64];
            snprintf(t, sizeof(t), LV_SYMBOL_WARNING "  Driver at %.0f C - let it cool",
                     g_app.drv_temp_c);
            w_toast(t, COL_ORANGE, 3000);
            return;
        }
        const uint32_t tot = dyn_cycle_seconds() * g_dyn.cycles;
        char msg[224];
        snprintf(msg, sizeof(msg),
                 "Cycle %.0f to %.0f kg, %lu times?\n\n"
                 "About %luh %02lum unattended under load. Check the fixture is "
                 "clear and logging to \"%s\".",
                 g_dyn.bottom_kg, g_dyn.top_kg, (unsigned long)g_dyn.cycles,
                 (unsigned long)(tot / 3600), (unsigned long)((tot % 3600) / 60),
                 g_dyn.name);
        w_modal("Start cyclic test", msg, COL_BLUE, "Start", "Cancel", start_confirmed);
        return;
    }

    if (s_phase == DynPhase::Paused) {          // ---- CONTINUE ----
        // The rig is idle while paused, so this is a NEW run as far as the
        // checks are concerned: refused if the e-stop is in or it is too hot.
        if (!link_cmd_hold((s_resume == DynPhase::ToTop || s_resume == DynPhase::AtTop)
                           ? g_dyn.top_kg : g_dyn.bottom_kg)) return;
        s_paused_tot += millis() - s_paused_at;
        s_phase    = s_resume;
        s_phase_ms = millis();
        set_go_button();
        return;
    }

    // ---- PAUSE ----
    s_resume    = s_phase;
    s_phase     = DynPhase::Paused;
    s_paused_at = millis();
    link_cmd_stop();
    lv_label_set_text(s_live, "PAUSED - force is not being controlled");
    lv_obj_set_style_text_color(s_live, COL_ORANGE, 0);
    set_go_button();
}

static void stop_cb(lv_event_t* /*e*/) {
    if (s_phase != DynPhase::Idle) dyn_stop("Stopped - retracting", COL_RED);
    else                           link_cmd_stop_and_retract();
    w_toast("Stopped - retracting 5 mm", COL_RED, 2500);
}

// ---- jog / retract ---------------------------------------------------------
static bool jog_guard() {
    if (s_phase != DynPhase::Idle) {
        w_toast("Stop the test before jogging", COL_ORANGE, 2000);
        return false;
    }
    if (!g_app.link_ok) {
        w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000);
        return false;
    }
    if (g_app.estop) {
        w_toast(LV_SYMBOL_WARNING "  E-stop pressed", COL_RED, 2000);
        return false;
    }
    return true;
}
static void jog_back_cb(lv_event_t*) {
    if (!jog_guard()) return;
    link_cmd_jog(-1);
    w_toast("Jog Retract ~1 mm", COL_BLUE, 1200);
}
static void jog_fwd_cb(lv_event_t*) {
    if (!jog_guard()) return;
    link_cmd_jog(+1);
    w_toast("Jog Extend ~1 mm", COL_BLUE, 1200);
}
static void retract_confirmed() {
    link_cmd_backoff();
    w_toast("Retracting ~5 mm", COL_BLUE, 2000);
}
static void retract_cb(lv_event_t*) {
    if (!jog_guard()) return;
    w_modal("Retract 5 mm?",
            "The actuator will retract about 5 mm to unload the cell.",
            COL_BLUE, "Retract", "Cancel", retract_confirmed);
}

// ---- download dialog -------------------------------------------------------
static lv_obj_t*   s_dl_bg     = nullptr;
static lv_obj_t*   s_dl_status = nullptr;
static lv_timer_t* s_dl_timer  = nullptr;

static void dl_close() {
    if (s_dl_timer) { lv_timer_del(s_dl_timer); s_dl_timer = nullptr; }
    web_stop();
    if (s_dl_bg) { lv_obj_del(s_dl_bg); s_dl_bg = nullptr; s_dl_status = nullptr; }
}

static void dl_close_cb(lv_event_t* /*e*/) { dl_close(); }

static void dl_tick(lv_timer_t* /*t*/) {
    web_poll();
    if (!s_dl_status) return;
    const int n = web_client_count();
    char b[64];
    if (n > 0) {
        snprintf(b, sizeof(b), LV_SYMBOL_OK "  %d device connected", n);
        lv_label_set_text(s_dl_status, b);
        lv_obj_set_style_text_color(s_dl_status, COL_GREEN, 0);
    } else {
        lv_label_set_text(s_dl_status, "Waiting for a device to connect...");
        lv_obj_set_style_text_color(s_dl_status, COL_MUTED, 0);
    }
}

static void download_cb(lv_event_t* /*e*/) {
    if (s_phase != DynPhase::Idle) {
        w_toast("Stop the test before downloading", COL_ORANGE, 2500);
        return;
    }

    web_start();

    s_dl_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_dl_bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_dl_bg, 0, 0);
    lv_obj_set_style_bg_color(s_dl_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dl_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_dl_bg, 0, 0);
    lv_obj_set_style_pad_all(s_dl_bg, 0, 0);
    lv_obj_clear_flag(s_dl_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dl_bg, LV_OBJ_FLAG_CLICKABLE);

    const int DW = 580, DH = 452;
    lv_obj_t* box = theme_card(s_dl_bg);
    lv_obj_set_size(box, DW, DH);
    lv_obj_center(box);
    lv_obj_set_style_border_color(box, COL_BLUE, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_pad_all(box, 22, 0);

    const int IW = DW - 44;

    lv_obj_align(theme_label(box, "Download Data  (CSV)", FONT_LG, COL_TEXT),
                 LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* warn = lv_obj_create(box);
    lv_obj_set_size(warn, IW, 38);
    lv_obj_set_style_bg_color(warn, COL_ORANGE_BG, 0);
    lv_obj_set_style_bg_opa(warn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(warn, 9, 0);
    lv_obj_set_style_border_color(warn, COL_ORANGE, 0);
    lv_obj_set_style_border_width(warn, 1, 0);
    lv_obj_clear_flag(warn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_center(theme_label(warn, "Keep this window open until the download finishes",
                              FONT_XS, COL_ORANGE));

    lv_obj_align(theme_label(box, "1", FONT_SM, COL_BLUE), LV_ALIGN_TOP_LEFT, 0, 96);
    lv_obj_align(theme_label(box, "Connect to this Wi-Fi network", FONT_SM, COL_TEXT),
                 LV_ALIGN_TOP_LEFT, 22, 96);
    lv_obj_t* wifi = lv_obj_create(box);
    lv_obj_set_size(wifi, IW - 22, 66);
    lv_obj_set_style_bg_color(wifi, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(wifi, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi, 9, 0);
    lv_obj_set_style_border_width(wifi, 0, 0);
    lv_obj_clear_flag(wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 22, 116);
    lv_obj_align(theme_label(wifi, "NETWORK", FONT_XS, COL_MUTED), LV_ALIGN_TOP_LEFT, 0, 2);
    lv_obj_align(theme_label(wifi, web_ssid(), FONT_MD, COL_TEXT), LV_ALIGN_TOP_LEFT, 0, 24);
    lv_obj_align(theme_label(wifi, "PASSWORD", FONT_XS, COL_MUTED), LV_ALIGN_TOP_RIGHT, -70, 2);
    lv_obj_align(theme_label(wifi, web_pass(), FONT_MD, COL_TEXT), LV_ALIGN_TOP_RIGHT, -70, 24);

    lv_obj_align(theme_label(box, "2", FONT_SM, COL_BLUE), LV_ALIGN_TOP_LEFT, 0, 196);
    lv_obj_align(theme_label(box, "Open this address in a browser", FONT_SM, COL_TEXT),
                 LV_ALIGN_TOP_LEFT, 22, 196);
    lv_obj_t* addr = lv_obj_create(box);
    lv_obj_set_size(addr, IW - 22, 52);
    lv_obj_set_style_bg_color(addr, COL_BLUE_BG, 0);
    lv_obj_set_style_bg_opa(addr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(addr, 9, 0);
    lv_obj_set_style_border_color(addr, COL_BLUE, 0);
    lv_obj_set_style_border_width(addr, 2, 0);
    lv_obj_clear_flag(addr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(addr, LV_ALIGN_TOP_LEFT, 22, 216);
    char url[40];
    snprintf(url, sizeof(url), "http://%s", web_ip());
    lv_obj_center(theme_label(addr, url, FONT_LG, COL_TEXT));

    lv_obj_align(theme_label(box, "3", FONT_SM, COL_BLUE), LV_ALIGN_TOP_LEFT, 0, 280);
    lv_obj_align(theme_label(box, "Pick a file and press Download", FONT_SM, COL_TEXT),
                 LV_ALIGN_TOP_LEFT, 22, 280);

    char note[96];
    if (datalog_has_card())
        snprintf(note, sizeof(note), "%d file(s) on card  -  %lu MB free",
                 datalog_file_count(), (unsigned long)datalog_free_mb());
    else
        snprintf(note, sizeof(note), "No SD card - cards >= 32 GB must be FAT32");
    lv_obj_align(theme_label(box, note, FONT_XS, COL_FAINT), LV_ALIGN_TOP_LEFT, 44, 300);

    s_dl_status = theme_label(box, "Waiting for a device to connect...", FONT_XS, COL_MUTED);
    lv_obj_align(s_dl_status, LV_ALIGN_TOP_MID, 0, 326);

    lv_obj_t* done = w_button(box, "Done  -  closes hotspot", WBTN_PRIMARY,
                              FONT_MD, dl_close_cb, nullptr);
    lv_obj_set_size(done, IW, 48);
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_dl_timer = lv_timer_create(dl_tick, 50, nullptr);
}

// ===========================================================================
//  BUILD
// ===========================================================================
static void make_param(lv_obj_t* scr, int i, const char* cap, const char* unit) {
    lv_obj_t* b = w_button(scr, "", WBTN_SECONDARY, FONT_MD,
                           param_cb, (void*)(intptr_t)i);
    lv_obj_set_size(b, PARAM_W, PARAM_H);
    lv_obj_set_pos(b, PAD_EDGE + i * (PARAM_W + 10), PARAM_Y);
    lv_obj_set_style_radius(b, 11, 0);
    lv_obj_set_style_border_color(b, COL_CARD3, 0);
    lv_obj_set_style_border_width(b, 1, 0);

    lv_obj_t* capl = theme_label(b, cap, FONT_XS, COL_MUTED);
    lv_obj_set_width(capl, PARAM_W - 20);
    lv_label_set_long_mode(capl, LV_LABEL_LONG_CLIP);
    lv_obj_align(capl, LV_ALIGN_TOP_LEFT, 10, 10);

    s_pv[i] = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(s_pv[i], FONT_LG, 0);
    lv_obj_align(s_pv[i], LV_ALIGN_TOP_LEFT, 10, 36);

    if (unit && unit[0]) {
        lv_obj_t* u = theme_label(b, unit, FONT_XS, COL_BLUE);
        lv_obj_align(u, LV_ALIGN_TOP_RIGHT, -10, 48);
    }
}

lv_obj_t* screen_dynamic_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Dynamic Loading", true);

    make_param(scr, 0, "BOTTOM LOAD", "kg");
    make_param(scr, 1, "TOP LOAD",    "kg");
    make_param(scr, 2, "BOTTOM DWELL","s");
    make_param(scr, 3, "TOP DWELL",   "s");
    make_param(scr, 4, "CYCLES",      "");

    lv_obj_t* sq = theme_label(scr, "SQUARE WAVE ONLY", FONT_XS, COL_ORANGE);
    lv_obj_set_style_text_letter_space(sq, 1, 0);
    lv_obj_align(sq, LV_ALIGN_TOP_LEFT, PAD_EDGE + 4, PARAM_Y + PARAM_H + 8);

    s_info = theme_label(scr, "", FONT_XS, COL_FAINT);
    lv_obj_set_width(s_info, 430);
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_info, LV_ALIGN_TOP_RIGHT, -(PAD_EDGE + 4), PARAM_Y + PARAM_H + 8);

    // ---- Chart ------------------------------------------------------------
    lv_obj_t* card = theme_card(scr);
    lv_obj_set_size(card, CHART_W, CHART_H);
    lv_obj_set_pos(card, PAD_EDGE, CHART_Y);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_align(w_caption(card, "FORCE vs TIME"), LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* nb = w_button(card, "", WBTN_GHOST, FONT_XS, name_cb, nullptr);
    lv_obj_set_size(nb, 168, 26);
    lv_obj_align(nb, LV_ALIGN_TOP_LEFT, 130, -2);
    s_name_lbl = lv_obj_get_child(nb, 0);
    lv_obj_set_style_text_color(s_name_lbl, COL_BLUE, 0);

    s_live = theme_label(card, "Idle", FONT_SM, COL_MUTED);
    lv_obj_align(s_live, LV_ALIGN_TOP_RIGHT, 0, -2);

    s_chart = lv_chart_create(card);
    lv_obj_set_size(s_chart, CHART_W - 24, CHART_H - 48);
    lv_obj_align(s_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINTS);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_line_color(s_chart, COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_div_line_count(s_chart, 5, 0);
    s_ser = lv_chart_add_series(s_chart, COL_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    rescale_chart();

    // ---- Jog column, beside the chart -------------------------------------
    {
        struct JogDef { const char* top; const char* sub; lv_event_cb_t cb; };
        const JogDef defs[3] = {
            { LV_SYMBOL_MINUS "  Jog", "retract 1mm", jog_back_cb },
            { LV_SYMBOL_PLUS  "  Jog", "extend 1mm",  jog_fwd_cb  },
            { "Retract",               "5 mm",        retract_cb  },
        };
        for (int i = 0; i < 3; i++) {
            lv_obj_t* b = w_button(scr, defs[i].top, WBTN_SECONDARY, FONT_SM,
                                   defs[i].cb, nullptr);
            lv_obj_set_size(b, JOG_W, JOG_H);
            lv_obj_set_pos(b, JOG_X, CHART_Y + i * (JOG_H + 10));
            lv_obj_set_style_border_color(b, COL_CARD3, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_CENTER, 0, -8);
            lv_obj_align(theme_label(b, defs[i].sub, FONT_XS, COL_FAINT),
                         LV_ALIGN_CENTER, 0, 12);
            s_jog_btn[i] = b;
        }
    }

    // ---- Bottom row -------------------------------------------------------
    // Stats card, two rows of three:
    //   CYCLE    ELAPSED   PEAK         (FONT_MD values)
    //   CURRENT  DRV TEMP  PEAK I       (FONT_SM values, rig rev 5)
    const int STAT_W = 330;
    lv_obj_t* stats = theme_card(scr);
    lv_obj_set_size(stats, STAT_W, BOT_H);
    lv_obj_set_pos(stats, PAD_EDGE, BOT_Y);
    lv_obj_set_style_pad_all(stats, 12, 0);

    const int seg = (STAT_W - 24) / 3;
    const char* caps1[3] = { "CYCLE", "ELAPSED", "PEAK" };
    lv_obj_t** vals1[3] = { &s_cycle_lbl, &s_elapsed_lbl, &s_peak_lbl };
    for (int i = 0; i < 3; i++) {
        lv_obj_align(w_caption(stats, caps1[i]), LV_ALIGN_TOP_LEFT, seg * i, 0);
        lv_obj_t* v = theme_label(stats, "--", FONT_MD, COL_TEXT);
        lv_obj_align(v, LV_ALIGN_TOP_LEFT, seg * i, 17);
        *vals1[i] = v;
    }

    lv_obj_align(theme_separator(stats, STAT_W - 24), LV_ALIGN_TOP_LEFT, 0, 45);

    const char* caps2[3] = { "CURRENT", "DRV TEMP", "PEAK I" };
    lv_obj_t** vals2[3] = { &s_cur_lbl, &s_temp_lbl, &s_pki_lbl };
    for (int i = 0; i < 3; i++) {
        lv_obj_align(w_caption(stats, caps2[i]), LV_ALIGN_TOP_LEFT, seg * i, 51);
        lv_obj_t* v = theme_label(stats, "--", FONT_SM, COL_TEXT);
        lv_obj_align(v, LV_ALIGN_TOP_LEFT, seg * i, 68);
        *vals2[i] = v;
    }

    int x = PAD_EDGE + STAT_W + 10;
    lv_obj_t* dl = w_button(scr, LV_SYMBOL_DOWNLOAD "\nDownload", WBTN_SECONDARY,
                            FONT_MD, download_cb, nullptr);
    lv_obj_set_size(dl, 124, BOT_H);
    lv_obj_set_pos(dl, x, BOT_Y);
    lv_obj_set_style_border_color(dl, COL_BLUE, 0);
    lv_obj_set_style_border_width(dl, 2, 0);
    lv_obj_set_style_text_align(lv_obj_get_child(dl, 0), LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(dl, 0), COL_BLUE, 0);

    x += 124 + 10;
    s_go_btn = w_button(scr, LV_SYMBOL_PLAY "  START", WBTN_SUCCESS,
                        FONT_MD, go_cb, nullptr);
    lv_obj_set_size(s_go_btn, 146, BOT_H);
    lv_obj_set_pos(s_go_btn, x, BOT_Y);

    x += 146 + 10;
    lv_obj_t* sp = w_button(scr, LV_SYMBOL_STOP "  STOP", WBTN_DANGER,
                            FONT_MD, stop_cb, nullptr);
    lv_obj_set_size(sp, SCREEN_W - PAD_EDGE - x, BOT_H);
    lv_obj_set_pos(sp, x, BOT_Y);

    paint_params();
    return scr;
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz)
// ---------------------------------------------------------------------------
void screen_dynamic_refresh() {
    if (!s_cycle_lbl) return;

    char b[32];
    snprintf(b, sizeof(b), "%lu / %lu",
             (unsigned long)s_cycle, (unsigned long)g_dyn.cycles);
    lv_label_set_text(s_cycle_lbl, b);

    uint32_t el = 0;
    if (s_phase != DynPhase::Idle)
        el = (millis() - s_run_start - s_paused_tot) / 1000;
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
    lv_label_set_text(s_elapsed_lbl, b);

    snprintf(b, sizeof(b), "%.1f kg", s_peak);
    lv_label_set_text(s_peak_lbl, b);

    // ---- Drive row (rig rev 5 telemetry) ----------------------------------
    const bool drive = g_app.link_ok && g_app.has_drive_data;
    if (drive) {
        snprintf(b, sizeof(b), "%+.1f A", g_app.current_a);
        lv_label_set_text(s_cur_lbl, b);
        lv_obj_set_style_text_color(s_cur_lbl,
            fabsf(g_app.current_a) >= RIG_I_TRIP_A ? COL_RED : COL_TEXT, 0);

        if (g_app.drv_temp_ok) {
            const int lev = drv_temp_level(g_app.drv_temp_c);
            snprintf(b, sizeof(b), "%.1f C", g_app.drv_temp_c);
            lv_label_set_text(s_temp_lbl, b);
            lv_obj_set_style_text_color(s_temp_lbl,
                lev == 2 ? COL_RED : lev == 1 ? COL_ORANGE : COL_GREEN, 0);
        } else {
            lv_label_set_text(s_temp_lbl, "fault");
            lv_obj_set_style_text_color(s_temp_lbl, COL_RED, 0);
        }

        snprintf(b, sizeof(b), "%.1f A", s_peak_i);
        lv_label_set_text(s_pki_lbl, b);
    } else {
        lv_label_set_text(s_cur_lbl,  "--");
        lv_label_set_text(s_temp_lbl, "--");
        lv_obj_set_style_text_color(s_cur_lbl,  COL_MUTED, 0);
        lv_obj_set_style_text_color(s_temp_lbl, COL_MUTED, 0);
        snprintf(b, sizeof(b), "%.1f A", s_peak_i);
        lv_label_set_text(s_pki_lbl, b);
    }

    if (s_phase == DynPhase::Idle) {
        if (!g_app.link_ok) {
            lv_label_set_text(s_live, "No link to rig");
            lv_obj_set_style_text_color(s_live, COL_ORANGE, 0);
        }
        w_button_enable(s_go_btn, g_app.link_ok && !g_app.estop);
    }

    const bool can_jog = (s_phase == DynPhase::Idle) && g_app.link_ok && !g_app.estop;
    for (int i = 0; i < 3; i++)
        if (s_jog_btn[i]) w_button_enable(s_jog_btn[i], can_jog);
}
