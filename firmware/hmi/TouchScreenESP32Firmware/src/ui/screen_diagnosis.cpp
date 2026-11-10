#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  DIAGNOSIS
//
//  Two test panels (Load Cell, Actuator), a Driver & Power strip, and a
//  link-health strip along the bottom.
//
//  HONEST SCOPE: everything shown here comes from telemetry the rig actually
//  sends. Rig rev 5 (PCB rev 2) added motor current, driver temperature,
//  e-stop and driver-fault state; with an older rig those cells read "--"
//  and say so, rather than showing plausible-looking zeros.
//
//  The limit-switch row was removed: no switches are fitted, the actuator
//  stops itself at its ends. The rig detects that from current instead and
//  reports it as "end of travel", shown on the Actuator chip and row.
//
//  10/2026: the strip shows LIVE motor current (was peak since boot).
//
//  NOTE: the built-in Montserrat fonts are ASCII only, so temperatures are
//  written "41.8 C" -- a degree sign would render as a box.
//
//  ACTIONS map to real rig commands:
//    Read cell   -> 'R'     Jog - / +  -> '-' / '+'     Retract -> 'B'
// ===========================================================================

// ---- Layout -------------------------------------------------------------
static const int CARD_W  = (CONTENT_W - PAD_GAP) / 2;      // 380
static const int CARD_H  = 232;
static const int DRV_Y   = CONTENT_Y + CARD_H + PAD_GAP;   // 306
static const int DRV_H   = 84;
static const int LINK_Y  = DRV_Y + DRV_H + PAD_GAP;        // 402
static const int LINK_H  = SCREEN_H - PAD_EDGE - LINK_Y;   // 64

// Temperature bar scale, C.
static const float TBAR_MIN = 20.0f;
static const float TBAR_MAX = 90.0f;
static const int   TBAR_W   = 160;

// ---- Widget handles -------------------------------------------------------
static lv_obj_t* s_cell_reading = nullptr;
static lv_obj_t* s_cell_target  = nullptr;
static lv_obj_t* s_cell_state   = nullptr;
static WChip     s_cell_chip;

static lv_obj_t* s_act_regime   = nullptr;
static lv_obj_t* s_act_pwm      = nullptr;
static lv_obj_t* s_act_travel   = nullptr;
static WChip     s_act_chip;

static lv_obj_t* s_temp_val     = nullptr;
static lv_obj_t* s_temp_bar     = nullptr;
static lv_obj_t* s_drv_val      = nullptr;
static lv_obj_t* s_drv_sub      = nullptr;
static lv_obj_t* s_es_val       = nullptr;
static lv_obj_t* s_es_sub       = nullptr;
static lv_obj_t* s_i_val        = nullptr;   // live motor current
static lv_obj_t* s_i_sub        = nullptr;

static lv_obj_t* s_link_state   = nullptr;
static lv_obj_t* s_link_age     = nullptr;
static lv_obj_t* s_link_frames  = nullptr;
static lv_obj_t* s_link_uptime  = nullptr;

// ---------------------------------------------------------------------------
//  ACTION CALLBACKS
// ---------------------------------------------------------------------------
static bool require_link() {
    if (g_app.link_ok) return true;
    w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000);
    return false;
}

static bool require_motion_ok() {
    if (!require_link()) return false;
    if (g_app.estop) {
        w_toast(LV_SYMBOL_WARNING "  E-stop pressed", COL_RED, 2000);
        return false;
    }
    return true;
}

static void read_cell_cb(lv_event_t* /*e*/) {
    if (!require_link()) return;
    link_cmd_read();
    w_toast("Reading load cell...", COL_BLUE, 1500);
}

static void jog_fwd_cb(lv_event_t* /*e*/) {
    if (!require_motion_ok()) return;
    if (g_app.eot & 0x01) { w_toast("At extend end of travel", COL_ORANGE, 2000); return; }
    link_cmd_jog(+1);
    w_toast("Jog Extend ~1 mm", COL_BLUE, 1500);
}

static void jog_back_cb(lv_event_t* /*e*/) {
    if (!require_motion_ok()) return;
    if (g_app.eot & 0x02) { w_toast("At retract end of travel", COL_ORANGE, 2000); return; }
    link_cmd_jog(-1);
    w_toast("Jog Retract ~1 mm", COL_BLUE, 1500);
}

static void backoff_confirmed() {
    link_cmd_backoff();
    w_toast("Retracting ~5 mm", COL_BLUE, 2000);
}

static void backoff_cb(lv_event_t* /*e*/) {
    if (!require_motion_ok()) return;
    w_modal("Retract 5 mm?",
            "The actuator will retract about 5 mm to unload the cell.",
            COL_BLUE, "Retract", "Cancel", backoff_confirmed);
}

// ---------------------------------------------------------------------------
//  BUILDERS
// ---------------------------------------------------------------------------
static lv_obj_t* build_card(lv_obj_t* scr, int x, const char* title,
                            const char* const keys[3],
                            lv_obj_t** out_vals, WChip* out_chip) {
    lv_obj_t* card = theme_card(scr);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, x, CONTENT_Y);

    const int inner_w = CARD_W - 2 * PAD_EDGE;

    lv_obj_align(theme_label(card, title, FONT_MD, COL_TEXT),
                 LV_ALIGN_TOP_LEFT, 0, 2);

    *out_chip = w_chip(card, "...", COL_FAINT, 118);
    lv_obj_align(out_chip->cont, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_align(theme_separator(card, inner_w), LV_ALIGN_TOP_LEFT, 0, 42);

    for (int i = 0; i < 3; i++) {
        out_vals[i] = w_kv_row(card, keys[i], 58 + i * 30, inner_w);
    }
    return card;
}

// One cell of the Driver & Power strip: caption, value, optional sub line.
static void drv_cell(lv_obj_t* strip, int x, const char* cap,
                     lv_obj_t** val, lv_obj_t** sub) {
    lv_obj_align(w_caption(strip, cap), LV_ALIGN_TOP_LEFT, x, 0);
    *val = theme_label(strip, "--", FONT_MD, COL_TEXT);
    lv_obj_align(*val, LV_ALIGN_TOP_LEFT, x, 17);
    if (sub) {
        *sub = theme_label(strip, "", FONT_XS, COL_FAINT);
        lv_obj_align(*sub, LV_ALIGN_TOP_LEFT, x, 44);
    }
}

// A 2 x 12 px tick on the temperature bar at temperature `c`.
static void temp_tick(lv_obj_t* strip, int bar_x, float c, lv_color_t col) {
    lv_obj_t* t = lv_obj_create(strip);
    lv_obj_set_size(t, 2, 12);
    lv_obj_set_style_bg_color(t, col, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 0, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    const int x = bar_x + (int)((c - TBAR_MIN) / (TBAR_MAX - TBAR_MIN) * TBAR_W);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, x, 44);
}

// ---------------------------------------------------------------------------
//  BUILD
// ---------------------------------------------------------------------------
lv_obj_t* screen_diagnosis_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Diagnosis", true);

    const int inner_w = CARD_W - 2 * PAD_EDGE;   // 352

    // ================= LOAD CELL CARD ====================================
    static const char* const cell_keys[3] = { "Live reading", "Rig setpoint", "Rig state" };
    lv_obj_t* cell_vals[3];
    lv_obj_t* cell = build_card(scr, PAD_EDGE, "Load Cell",
                                cell_keys, cell_vals, &s_cell_chip);
    s_cell_reading = cell_vals[0];
    s_cell_target  = cell_vals[1];
    s_cell_state   = cell_vals[2];

    lv_obj_t* rb = w_button(cell, "Read Load Cell", WBTN_PRIMARY, FONT_MD,
                            read_cell_cb, nullptr);
    lv_obj_set_size(rb, inner_w, 52);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, 0);

    // ================= ACTUATOR CARD =====================================
    static const char* const act_keys[3] = { "Control regime", "Applied PWM", "End of travel" };
    lv_obj_t* act_vals[3];
    const int act_x = PAD_EDGE + CARD_W + PAD_GAP;
    lv_obj_t* act = build_card(scr, act_x, "Actuator",
                               act_keys, act_vals, &s_act_chip);
    s_act_regime  = act_vals[0];
    s_act_pwm     = act_vals[1];
    s_act_travel  = act_vals[2];

    // One row of three buttons: Jog -, Jog +, Retract 5 mm.
    const int bw = (inner_w - 2 * 8) / 3;
    lv_obj_t* jb = w_button(act, LV_SYMBOL_MINUS "  Jog", WBTN_SECONDARY, FONT_SM,
                            jog_back_cb, nullptr);
    lv_obj_set_size(jb, bw, 52);
    lv_obj_align(jb, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t* jf = w_button(act, LV_SYMBOL_PLUS "  Jog", WBTN_SECONDARY, FONT_SM,
                            jog_fwd_cb, nullptr);
    lv_obj_set_size(jf, bw, 52);
    lv_obj_align(jf, LV_ALIGN_BOTTOM_LEFT, bw + 8, 0);

    lv_obj_t* bo = w_button(act, "Retract\n5 mm", WBTN_PRIMARY, FONT_SM,
                            backoff_cb, nullptr);
    lv_obj_set_size(bo, bw, 52);
    lv_obj_align(bo, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_align(lv_obj_get_child(bo, 0), LV_TEXT_ALIGN_CENTER, 0);

    // ================= DRIVER & POWER STRIP ==============================
    lv_obj_t* drv = theme_card(scr);
    lv_obj_set_size(drv, CONTENT_W, DRV_H);
    lv_obj_set_pos(drv, PAD_EDGE, DRV_Y);
    lv_obj_set_style_pad_all(drv, 12, 0);

    const int seg = (CONTENT_W - 24) / 4;   // 187

    // Cell 1: temperature with a bar marked at the warning and trip levels.
    drv_cell(drv, 4, "DRIVER TEMP", &s_temp_val, nullptr);
    s_temp_bar = lv_bar_create(drv);
    lv_obj_set_size(s_temp_bar, TBAR_W, 6);
    lv_obj_align(s_temp_bar, LV_ALIGN_TOP_LEFT, 4, 47);
    lv_bar_set_range(s_temp_bar, 0, 100);
    lv_obj_set_style_bg_color(s_temp_bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_temp_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_temp_bar, COL_GREEN, LV_PART_INDICATOR);
    temp_tick(drv, 4, RIG_TEMP_WARN_C, COL_ORANGE);
    temp_tick(drv, 4, RIG_TEMP_TRIP_C, COL_RED);

    drv_cell(drv, seg * 1 + 4, "MOTOR DRIVER",   &s_drv_val, &s_drv_sub);
    drv_cell(drv, seg * 2 + 4, "EMERGENCY STOP", &s_es_val,  &s_es_sub);
    drv_cell(drv, seg * 3 + 4, "MOTOR CURRENT",  &s_i_val,   &s_i_sub);

    // ================= LINK STRIP ========================================
    lv_obj_t* strip = theme_card(scr);
    lv_obj_set_size(strip, CONTENT_W, LINK_H);
    lv_obj_set_pos(strip, PAD_EDGE, LINK_Y);
    lv_obj_set_style_pad_all(strip, 10, 0);

    const char* const strip_keys[4] = { "LINK", "LAST FRAME", "FRAMES", "HMI UPTIME" };
    lv_obj_t** strip_vals[4] = { &s_link_state, &s_link_age,
                                 &s_link_frames, &s_link_uptime };
    const int lseg = (CONTENT_W - 20) / 4;

    for (int i = 0; i < 4; i++) {
        lv_obj_t* cap = w_caption(strip, strip_keys[i]);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, lseg * i + 8, 0);

        lv_obj_t* v = theme_label(strip, "--", FONT_SM, COL_TEXT);
        lv_obj_align(v, LV_ALIGN_TOP_LEFT, lseg * i + 8, 18);
        *strip_vals[i] = v;
    }

    return scr;
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz)
// ---------------------------------------------------------------------------
static void set_val(lv_obj_t* l, const char* txt, lv_color_t col) {
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
}

void screen_diagnosis_refresh() {
    if (!s_cell_reading) return;

    char b[48];
    const bool live  = g_app.link_ok;
    const bool drive = live && g_app.has_drive_data;   // rig rev 5 fields present

    // ================= LOAD CELL =========================================
    if (live) {
        char f[16];
        snprintf(b, sizeof(b), "%s %s",
                 fmt_force(g_app.force_kg, f, sizeof(f)), unit_str());
        lv_label_set_text(s_cell_reading, b);

        snprintf(b, sizeof(b), "%.1f %s",
                 force_in_units(g_app.target_kg), unit_str());
        lv_label_set_text(s_cell_target, b);
    } else {
        lv_label_set_text(s_cell_reading, "--");
        lv_label_set_text(s_cell_target, "--");
    }

    const char* state_txt;
    switch (g_app.rig) {
        case RigState::Idle:    state_txt = "Idle";    break;
        case RigState::Running: state_txt = "Running"; break;
        case RigState::Fault:   state_txt = "FAULT";   break;
        default:                state_txt = "Unknown"; break;
    }
    lv_label_set_text(s_cell_state, state_txt);

    if (!live)                             w_chip_set(s_cell_chip, "No link", COL_ORANGE);
    else if (g_app.rig == RigState::Fault) w_chip_set(s_cell_chip, "Fault",   COL_RED);
    else                                   w_chip_set(s_cell_chip, "OK",      COL_GREEN);

    // ================= ACTUATOR ==========================================
    const char* reg_txt;
    switch (g_app.regime) {
        case RigRegime::Approach: reg_txt = "Approach"; break;
        case RigRegime::Contact:  reg_txt = "Contact";  break;
        default:                  reg_txt = "--";       break;
    }
    lv_label_set_text(s_act_regime, reg_txt);

    if (live) {
        snprintf(b, sizeof(b), "%+d  (%s)", g_app.pwm,
                 g_app.pwm > 0 ? "push" : (g_app.pwm < 0 ? "retract" : "stopped"));
        lv_label_set_text(s_act_pwm, b);
    } else {
        lv_label_set_text(s_act_pwm, "--");
    }

    if (!live)                  set_val(s_act_travel, "--",          COL_MUTED);
    else if (g_app.eot == 0x01) set_val(s_act_travel, "Extend end",  COL_ORANGE);
    else if (g_app.eot == 0x02) set_val(s_act_travel, "Retract end", COL_ORANGE);
    else if (g_app.eot != 0)    set_val(s_act_travel, "Both ends?",  COL_RED);
    else                        set_val(s_act_travel, "Free",        COL_TEXT);

    const bool at_end = (g_app.eot != 0) || (g_app.limits != 0);
    const int  tlev   = (drive && g_app.drv_temp_ok) ? drv_temp_level(g_app.drv_temp_c) : 0;

    if (!live)                             w_chip_set(s_act_chip, "No link",  COL_ORANGE);
    else if (g_app.estop)                  w_chip_set(s_act_chip, "E-STOP",   COL_RED);
    else if (g_app.drv_fault)              w_chip_set(s_act_chip, "Driver",   COL_RED);
    else if (g_app.rig == RigState::Fault) w_chip_set(s_act_chip, "Fault",    COL_RED);
    else if (tlev >= 1)                    w_chip_set(s_act_chip, "Hot",      COL_ORANGE);
    else if (g_app.eot == 0x01)            w_chip_set(s_act_chip, "Ext. end", COL_ORANGE);
    else if (g_app.eot == 0x02)            w_chip_set(s_act_chip, "Ret. end", COL_ORANGE);
    else if (at_end)                       w_chip_set(s_act_chip, "At end",   COL_ORANGE);
    else                                   w_chip_set(s_act_chip, "OK",       COL_GREEN);

    // ================= DRIVER & POWER ====================================
    // Temperature
    if (drive && g_app.drv_temp_ok) {
        const lv_color_t c = (tlev == 2) ? COL_RED : (tlev == 1) ? COL_ORANGE : COL_TEXT;
        snprintf(b, sizeof(b), "%.1f C", g_app.drv_temp_c);
        set_val(s_temp_val, b, c);
        int pct = (int)((g_app.drv_temp_c - TBAR_MIN) / (TBAR_MAX - TBAR_MIN) * 100.0f);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(s_temp_bar, pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_temp_bar,
            (tlev == 2) ? COL_RED : (tlev == 1) ? COL_ORANGE : COL_GREEN, LV_PART_INDICATOR);
    } else if (drive) {
        set_val(s_temp_val, "Sensor fault", COL_RED);
        lv_bar_set_value(s_temp_bar, 0, LV_ANIM_OFF);
    } else {
        set_val(s_temp_val, "--", COL_MUTED);
        lv_bar_set_value(s_temp_bar, 0, LV_ANIM_OFF);
    }

    // Motor driver (G2 FLT pin)
    if (!drive) {
        set_val(s_drv_val, "--", COL_MUTED);
        lv_label_set_text(s_drv_sub, live ? "needs rig rev 5" : "");
    } else if (g_app.estop) {
        set_val(s_drv_val, "No power", COL_ORANGE);
        lv_label_set_text(s_drv_sub, "cut by the e-stop");
    } else if (g_app.drv_fault) {
        set_val(s_drv_val, "FAULT", COL_RED);
        lv_label_set_text(s_drv_sub, "G2 fault pin active");
    } else {
        set_val(s_drv_val, "OK", COL_GREEN);
        lv_label_set_text(s_drv_sub, "G2 18v25, fault pin clear");
    }

    // E-stop
    if (!drive) {
        set_val(s_es_val, "--", COL_MUTED);
        lv_label_set_text(s_es_sub, "");
    } else if (g_app.estop) {
        set_val(s_es_val, "PRESSED", COL_RED);
        lv_label_set_text(s_es_sub, "motor power cut");
    } else {
        set_val(s_es_val, "Released", COL_GREEN);
        lv_label_set_text(s_es_sub, "switch closed");
    }

    // Live motor current (the peak-since-boot figure was dropped: a live
    // value is what tells you the actuator is working right now).
    if (drive) {
        const float a  = g_app.current_a;
        const float aa = fabsf(a);
        snprintf(b, sizeof(b), "%.2f A", aa);
        set_val(s_i_val, b, aa >= RIG_I_TRIP_A ? COL_RED
                          : aa >= 0.75f * RIG_I_TRIP_A ? COL_ORANGE : COL_TEXT);
        snprintf(b, sizeof(b), "%s, trip %.0f A",
                 aa < 0.05f ? "idle" : (a > 0 ? "push" : "retract"), RIG_I_TRIP_A);
        lv_label_set_text(s_i_sub, b);
    } else {
        set_val(s_i_val, "--", COL_MUTED);
        lv_label_set_text(s_i_sub, live ? "needs rig rev 5" : "");
    }

    // ================= LINK STRIP ========================================
    lv_label_set_text(s_link_state, live ? "Connected" : "Offline");
    lv_obj_set_style_text_color(s_link_state, live ? COL_GREEN : COL_ORANGE, 0);

    const uint32_t age = link_age_ms();
    if (age == 0xFFFFFFFF) snprintf(b, sizeof(b), "never");
    else if (age < 10000)  snprintf(b, sizeof(b), "%lu ms", (unsigned long)age);
    else                   snprintf(b, sizeof(b), "%lu s", (unsigned long)(age / 1000));
    lv_label_set_text(s_link_age, b);

    snprintf(b, sizeof(b), "%lu ok / %lu bad",
             (unsigned long)g_app.frames_rx, (unsigned long)g_app.bad_frames);
    lv_label_set_text(s_link_frames, b);

    const uint32_t up = millis() / 1000;
    snprintf(b, sizeof(b), "%lu:%02lu:%02lu",
             (unsigned long)(up / 3600),
             (unsigned long)((up / 60) % 60),
             (unsigned long)(up % 60));
    lv_label_set_text(s_link_uptime, b);
}
