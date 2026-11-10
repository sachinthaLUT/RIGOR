#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  STATIC TESTING
//
//  Left  : seven preset loads + a Custom slot (keypad entry), then a row of
//          jog / back-off controls for positioning the fixture.
//  Right : target, run stage, live force, motor current + driver temperature,
//          progress, and START/STOP.
//
//  WHY SELECT-THEN-START
//  A single stray touch on "70 kg" should not drive the actuator. Tapping a
//  load only ARMS it; the operator commits with START. Once running, that
//  same button becomes STOP so there is always one obvious way out.
//
//  WHY NO 90/100 kg PRESET
//  The rig refuses anything above MAX_SETPOINT_KG (90). A 100 kg button could
//  only ever be rejected, so it is gone; 90 is reachable through Custom,
//  which validates against RIG_MAX_KG.
//
//  STAGE DERIVATION (needs no rig changes)
//    regime=APPR                       -> Moving   (creeping through free air)
//    regime=CONT and |err| >  deadband -> Ramping  (force loop closing in)
//    regime=CONT and |err| <= deadband -> Holding  (settled at target)
//
//  DRIVE LINE (10/2026): live motor current and driver temperature sit on the
//  LIVE FORCE caption row, amber at the warning level and red at the trip
//  level, so a long hold that is cooking the driver is visible here and not
//  only on the Diagnosis screen.
// ===========================================================================

// ---- Preset loads, kg. Edit this list to change the buttons. --------------
// Keep it to 7: the 8th cell in the 2x4 grid is the Custom slot.
static const float PRESETS[] = { 10, 20, 30, 40, 50, 60, 70 };
static const int   PRESET_N  = sizeof(PRESETS) / sizeof(PRESETS[0]);
#define CUSTOM_INDEX PRESET_N          // the Custom tile's slot index

// Loads at or above this ask for confirmation (toggle in Settings).
static const float CONFIRM_ABOVE_KG = 50.0f;

// ---- Layout ---------------------------------------------------------------
static const int LEFT_W    = 416;                                // preset column
static const int BTN_COLS  = 2;
static const int BTN_ROWS  = 4;
static const int BTN_W     = (LEFT_W - PAD_GAP) / BTN_COLS;      // = 202
static const int JOG_H     = 68;                                 // jog row height
static const int GRID_H    = CONTENT_H - JOG_H - 10;
static const int BTN_H     = (GRID_H - (BTN_ROWS - 1) * 10) / BTN_ROWS;
static const int RIGHT_X   = PAD_EDGE + LEFT_W + PAD_GAP;
static const int RIGHT_W   = SCREEN_W - RIGHT_X - PAD_EDGE;

// ---- Widget handles -------------------------------------------------------
static lv_obj_t* s_preset_btn[PRESET_N + 1] = { nullptr };   // +1 for Custom
static lv_obj_t* s_custom_lbl   = nullptr;   // the Custom tile's big label
static lv_obj_t* s_custom_sub   = nullptr;   // "tap to enter" / current value
static lv_obj_t* s_target_lbl   = nullptr;
static lv_obj_t* s_action_btn   = nullptr;   // START / STOP
static lv_obj_t* s_progress     = nullptr;
static lv_obj_t* s_progress_lbl = nullptr;
static lv_obj_t* s_drive_lbl    = nullptr;   // "2.41 A   41.8 C"
static WReadout  s_readout;
static WStages   s_stages;

// ---- Local UI state -------------------------------------------------------
static int   s_selected     = 1;      // index into PRESETS, or CUSTOM_INDEX
static float s_custom_kg    = 0.0f;   // 0 = never set
static bool  s_was_running  = false;  // edge detection in refresh()

static const char* const STAGE_NAMES[] = { "Moving", "Ramping", "Holding" };

// The load the START button would actually command.
static float selected_kg() {
    return (s_selected == CUSTOM_INDEX) ? s_custom_kg : PRESETS[s_selected];
}

// ---------------------------------------------------------------------------
//  SELECTION PAINTING
//  Selected tile gets a white outline; the rest stay flat.
// ---------------------------------------------------------------------------
static void paint_selection() {
    for (int i = 0; i <= PRESET_N; i++) {
        if (!s_preset_btn[i]) continue;
        const bool sel = (i == s_selected);
        lv_obj_set_style_border_color(s_preset_btn[i],
                                      sel ? COL_TEXT : COL_BLUE, 0);
        // The Custom tile keeps a thin blue outline when unselected so it
        // still reads as "a different kind of thing" from the presets.
        int w = sel ? 3 : ((i == CUSTOM_INDEX) ? 2 : 0);
        lv_obj_set_style_border_width(s_preset_btn[i], w, 0);
    }

    if (s_target_lbl) {
        char b[24];
        if (selected_kg() <= 0.0f) {
            lv_label_set_text(s_target_lbl, "--");
        } else {
            snprintf(b, sizeof(b), "%.0f %s",
                     force_in_units(selected_kg()), unit_str());
            lv_label_set_text(s_target_lbl, b);
        }
    }
}

// Update the Custom tile's caption to show its stored value.
static void paint_custom() {
    if (!s_custom_sub) return;
    if (s_custom_kg > 0.0f) {
        char b[16];
        snprintf(b, sizeof(b), "%.1f kg", s_custom_kg);
        lv_label_set_text(s_custom_lbl, b);
        lv_label_set_text(s_custom_sub, "tap to change");
    } else {
        lv_label_set_text(s_custom_lbl, "Custom");
        lv_label_set_text(s_custom_sub, "tap to enter");
    }
}

// ---------------------------------------------------------------------------
//  CALLBACKS
// ---------------------------------------------------------------------------
static bool block_if_running() {
    if (g_app.rig == RigState::Running) {
        w_toast("Stop the run before changing load", COL_ORANGE, 2000);
        return true;
    }
    return false;
}

static void preset_cb(lv_event_t* e) {
    if (block_if_running()) return;
    s_selected = (int)(intptr_t)lv_event_get_user_data(e);
    paint_selection();
}

// Keypad accepted a value.
static void custom_entered(float kg) {
    s_custom_kg = kg;
    s_selected  = CUSTOM_INDEX;      // entering a value also selects it
    paint_custom();
    paint_selection();
}

static void custom_cb(lv_event_t* /*e*/) {
    if (block_if_running()) return;
    // One decimal: the rig parses "H20.5" happily, and finer than 0.1 kg is
    // below what the loop can hold anyway.
    w_keypad("Custom Load", "kg", s_custom_kg, 1.0f, RIG_MAX_KG, 1, custom_entered);
}

// ---- jog / back-off --------------------------------------------------------
static bool require_link() {
    if (g_app.link_ok) return true;
    w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000);
    return false;
}

static void jog_back_cb(lv_event_t* /*e*/) {
    if (block_if_running() || !require_link()) return;
    link_cmd_jog(-1);
    w_toast("Jog Retract ~1 mm", COL_BLUE, 1200);
}

static void jog_fwd_cb(lv_event_t* /*e*/) {
    if (block_if_running() || !require_link()) return;
    link_cmd_jog(+1);
    w_toast("Jog Extend ~1 mm", COL_BLUE, 1200);
}

static void backoff_confirmed() {
    link_cmd_backoff();
    w_toast("Retracting ~5 mm", COL_BLUE, 2000);
}

static void backoff_cb(lv_event_t* /*e*/) {
    if (block_if_running() || !require_link()) return;
    w_modal("Retract 5 mm?",
            "The actuator will retract about 5 mm to unload the cell.",
            COL_BLUE, "Retract", "Cancel", backoff_confirmed);
}

// ---- start / stop ----------------------------------------------------------
static void start_run() {
    if (link_cmd_hold(selected_kg())) w_toast("Run started", COL_GREEN, 1500);
}

static void action_cb(lv_event_t* /*e*/) {
    // Running -> this button is STOP. Checked first so STOP always works.
    if (g_app.rig == RigState::Running) {
        // Stop AND retract: ending a hold leaves the specimen sitting at full
        // load until someone thinks to back the actuator off. Unloading is
        // what you want in nearly every case, and it is easy to forget.
        link_cmd_stop_and_retract();
        w_toast("Stopped - retracting 5 mm", COL_RED, 2500);
        return;
    }

    if (g_app.rig == RigState::Fault) {
        w_modal("Rig is faulted",
                "Clear the latched fault before starting a new run.",
                COL_RED, "Reset rig", "Cancel", link_cmd_reset);
        return;
    }

    if (!require_link()) return;

    if (selected_kg() <= 0.0f) {
        w_toast("Enter a custom load first", COL_ORANGE, 2000);
        return;
    }

    if (g_settings.confirm_high && selected_kg() >= CONFIRM_ABOVE_KG) {
        char msg[144];
        snprintf(msg, sizeof(msg),
                 "Apply %.1f kg to the load cell?\n\n"
                 "Check the fixture is clear before continuing.",
                 selected_kg());
        w_modal("Confirm load", msg, COL_BLUE, "Start", "Cancel", start_run);
        return;
    }

    start_run();
}

// ---------------------------------------------------------------------------
//  BUILD
// ---------------------------------------------------------------------------
lv_obj_t* screen_static_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Static Testing", true);

    // ---- Preset grid (7 presets + Custom) --------------------------------
    for (int i = 0; i < PRESET_N; i++) {
        const int r = i / BTN_COLS, c = i % BTN_COLS;

        char txt[16];
        snprintf(txt, sizeof(txt), "%.0f kg", PRESETS[i]);

        lv_obj_t* b = w_button(scr, txt, WBTN_PRIMARY, FONT_LG,
                               preset_cb, (void*)(intptr_t)i);
        lv_obj_set_size(b, BTN_W, BTN_H);
        lv_obj_set_pos(b, PAD_EDGE + c * (BTN_W + PAD_GAP),
                          CONTENT_Y + r * (BTN_H + 10));
        lv_obj_set_style_radius(b, RADIUS_CARD, 0);
        s_preset_btn[i] = b;
    }

    // Custom tile: styled as an inset, not a solid preset, so it reads as a
    // different kind of control.
    {
        const int r = CUSTOM_INDEX / BTN_COLS, c = CUSTOM_INDEX % BTN_COLS;
        lv_obj_t* b = w_button(scr, "", WBTN_SECONDARY, FONT_MD,
                               custom_cb, nullptr);
        lv_obj_set_size(b, BTN_W, BTN_H);
        lv_obj_set_pos(b, PAD_EDGE + c * (BTN_W + PAD_GAP),
                          CONTENT_Y + r * (BTN_H + 10));
        lv_obj_set_style_radius(b, RADIUS_CARD, 0);

        // w_button() left an empty centred label as child 0 -- reuse it.
        s_custom_lbl = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_font(s_custom_lbl, FONT_MD, 0);
        lv_obj_set_style_text_color(s_custom_lbl, COL_BLUE, 0);
        lv_obj_align(s_custom_lbl, LV_ALIGN_CENTER, 0, -8);

        s_custom_sub = theme_label(b, "tap to enter", FONT_XS, COL_FAINT);
        lv_obj_align(s_custom_sub, LV_ALIGN_CENTER, 0, 14);

        s_preset_btn[CUSTOM_INDEX] = b;
        paint_custom();
    }

    // ---- Jog / Retract row ----------------------------------------------
    // Two-line labels: "- Jog" alone does not say which way it moves.
    {
        const int jy = CONTENT_Y + GRID_H + 10;
        const int jw = (LEFT_W - 2 * PAD_GAP) / 3;

        struct JogDef { const char* top; const char* sub; lv_event_cb_t cb; };
        const JogDef defs[3] = {
            { LV_SYMBOL_MINUS "  Jog", "Retract 1mm", jog_back_cb },
            { LV_SYMBOL_PLUS  "  Jog", "Extend 1mm",  jog_fwd_cb  },
            { "Retract",               "5mm",         backoff_cb  },
        };

        for (int i = 0; i < 3; i++) {
            lv_obj_t* b = w_button(scr, defs[i].top, WBTN_SECONDARY, FONT_SM,
                                   defs[i].cb, nullptr);
            lv_obj_set_size(b, jw, JOG_H);
            lv_obj_set_pos(b, PAD_EDGE + i * (jw + PAD_GAP), jy);

            lv_obj_t* top = lv_obj_get_child(b, 0);
            lv_obj_align(top, LV_ALIGN_CENTER, 0, -9);
            lv_obj_align(theme_label(b, defs[i].sub, FONT_XS, COL_FAINT),
                         LV_ALIGN_CENTER, 0, 12);
        }
    }

    // ---- Status panel -----------------------------------------------------
    lv_obj_t* card = theme_card(scr);
    lv_obj_set_size(card, RIGHT_W, CONTENT_H);
    lv_obj_set_pos(card, RIGHT_X, CONTENT_Y);

    const int inner_w = RIGHT_W - 2 * PAD_EDGE;

    lv_obj_align(w_caption(card, "TARGET LOAD"), LV_ALIGN_TOP_LEFT, 0, 6);
    s_target_lbl = theme_label(card, "--", FONT_LG, COL_TEXT);
    lv_obj_align(s_target_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Stage indicator lives in its own transparent box so w_stages() can lay
    // out against a known width.
    lv_obj_t* stage_box = lv_obj_create(card);
    lv_obj_set_size(stage_box, inner_w, 52);
    lv_obj_set_style_bg_opa(stage_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stage_box, 0, 0);
    lv_obj_set_style_pad_all(stage_box, 0, 0);
    lv_obj_clear_flag(stage_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(stage_box, LV_ALIGN_TOP_LEFT, 0, 48);
    s_stages = w_stages(stage_box, inner_w, STAGE_NAMES, 3);

    lv_obj_align(theme_separator(card, inner_w), LV_ALIGN_TOP_LEFT, 0, 112);

    lv_obj_align(w_caption(card, "LIVE FORCE"), LV_ALIGN_TOP_LEFT, 0, 126);

    // Motor current + driver temperature, right-aligned on the caption row.
    s_drive_lbl = theme_label(card, "--", FONT_XS, COL_MUTED);
    lv_obj_align(s_drive_lbl, LV_ALIGN_TOP_RIGHT, 0, 124);

    s_readout = w_readout(card, inner_w, 110, FONT_HUGE);
    lv_obj_align(s_readout.cont, LV_ALIGN_TOP_LEFT, 0, 146);

    s_progress = lv_bar_create(card);
    lv_obj_set_size(s_progress, inner_w, 8);
    lv_obj_set_style_bg_color(s_progress, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, COL_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress, 4, LV_PART_INDICATOR);
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
    lv_obj_align(s_progress, LV_ALIGN_TOP_LEFT, 0, 268);

    s_progress_lbl = theme_label(card, "Idle", FONT_XS, COL_MUTED);
    lv_obj_align(s_progress_lbl, LV_ALIGN_TOP_LEFT, 0, 284);

    s_action_btn = w_button(card, "START", WBTN_SUCCESS, FONT_LG,
                            action_cb, nullptr);
    lv_obj_set_size(s_action_btn, inner_w, 58);
    lv_obj_align(s_action_btn, LV_ALIGN_BOTTOM_MID, 0, 0);

    paint_selection();
    return scr;
}

// ---------------------------------------------------------------------------
//  DRIVE LINE  --  "2.41 A    41.8 C"  (ASCII only: no degree sign in the font)
// ---------------------------------------------------------------------------
static void paint_drive() {
    if (!g_app.link_ok || !g_app.has_drive_data) {
        lv_label_set_text(s_drive_lbl, g_app.link_ok ? "drive data: rig rev 5" : "--");
        lv_obj_set_style_text_color(s_drive_lbl, COL_FAINT, 0);
        return;
    }

    const float amps = fabsf(g_app.current_a);
    int lev = (amps >= RIG_I_TRIP_A) ? 2 : (amps >= 0.75f * RIG_I_TRIP_A) ? 1 : 0;

    char b[40];
    if (g_app.drv_temp_ok) {
        const int tl = drv_temp_level(g_app.drv_temp_c);
        if (tl > lev) lev = tl;
        snprintf(b, sizeof(b), "Motor %.2f A   Driver %.1f C", amps, g_app.drv_temp_c);
    } else {
        lev = 2;                                   // a dead thermistor is a fault
        snprintf(b, sizeof(b), "Motor %.2f A   Driver -- C", amps);
    }
    lv_label_set_text(s_drive_lbl, b);
    lv_obj_set_style_text_color(s_drive_lbl,
        lev == 2 ? COL_RED : lev == 1 ? COL_ORANGE : COL_MUTED, 0);
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz)
// ---------------------------------------------------------------------------
void screen_static_refresh() {
    if (!s_action_btn) return;   // screen not built yet

    const bool running = (g_app.rig == RigState::Running);

    // ---- Big force readout ------------------------------------------------
    if (g_app.link_ok) {
        char val[16], sub[32];
        fmt_force(g_app.force_kg, val, sizeof(val));

        // Secondary line always shows the OTHER unit, so both are visible.
        if (g_settings.units == Units::Newton)
            snprintf(sub, sizeof(sub), "%.2f kg", g_app.force_kg);
        else
            snprintf(sub, sizeof(sub), "%.1f N", kg_to_n(g_app.force_kg));

        w_readout_set(s_readout, val, unit_str(), sub);
        w_readout_set_color(s_readout, COL_TEXT, COL_BLUE);
    } else {
        // No link: dashes, NOT the last number we happened to hear. A frozen
        // but plausible reading is the dangerous failure mode here.
        w_readout_set(s_readout, "--", unit_str(), "no link");
        w_readout_set_color(s_readout, COL_FAINT, COL_FAINT);
    }

    paint_drive();

    // ---- Stage + progress -------------------------------------------------
    if (running && g_app.link_ok) {
        const float err = fabsf(g_app.target_kg - g_app.force_kg);

        int stage;
        if (g_app.regime == RigRegime::Approach) stage = 0;   // Moving
        else if (err > RIG_DEADBAND_KG)          stage = 1;   // Ramping
        else                                     stage = 2;   // Holding
        w_stages_set(s_stages, stage);

        int pct = 0;
        if (g_app.target_kg > 0.01f) {
            pct = (int)((g_app.force_kg / g_app.target_kg) * 100.0f);
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
        }
        lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);

        char msg[72];
        if (stage == 0) {
            snprintf(msg, sizeof(msg), "Approaching contact...");
            lv_obj_set_style_bg_color(s_progress, COL_BLUE, LV_PART_INDICATOR);
        } else if (stage == 1) {
            snprintf(msg, sizeof(msg), "Ramping to %.1f %s  -  %d%%",
                     force_in_units(g_app.target_kg), unit_str(), pct);
            lv_obj_set_style_bg_color(s_progress, COL_ORANGE, LV_PART_INDICATOR);
        } else {
            snprintf(msg, sizeof(msg), "Holding at %.1f %s",
                     force_in_units(g_app.target_kg), unit_str());
            lv_obj_set_style_bg_color(s_progress, COL_GREEN, LV_PART_INDICATOR);
        }
        lv_label_set_text(s_progress_lbl, msg);
    } else {
        w_stages_set(s_stages, -1);              // all pending
        lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
        lv_label_set_text(s_progress_lbl,
                          g_app.rig == RigState::Fault ? "Faulted - reset required"
                                                       : "Idle");
    }

    // ---- Action button ----------------------------------------------------
    // Only restyle when the run state actually flips, rather than 20x/second.
    if (running != s_was_running) {
        s_was_running = running;
        if (running) {
            w_button_set_text(s_action_btn, LV_SYMBOL_STOP "  STOP");
            w_button_set_style(s_action_btn, WBTN_DANGER);
        } else {
            w_button_set_text(s_action_btn, "START");
            w_button_set_style(s_action_btn, WBTN_SUCCESS);
        }
    }

    // START needs a link; STOP stays live if we are somehow still running.
    w_button_enable(s_action_btn, g_app.link_ok || running);
}
