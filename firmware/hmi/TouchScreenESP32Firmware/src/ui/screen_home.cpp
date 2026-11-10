#include <Arduino.h>   // millis(), Serial, etc.
#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <stdint.h>
#include <stdio.h>

// ===========================================================================
//  HOME
//
//  4 columns x 2 rows. Columns 0-2 navigate; column 3 is a live status panel
//  (top) and an always-reachable STOP (bottom).
//
//  WHY STOP AND NOT "POWER OFF"
//  This board is a display terminal -- it cannot cut power to anything, so a
//  Power Off button could only ever lie. STOP is genuinely useful: from any
//  screen, Home is one tap and STOP is the next.
// ===========================================================================

// ---- Layout ---------------------------------------------------------------
// Six tiles in 3x2, and the whole fourth column is a live status panel.
//
// WHY NO "POWER OFF" OR "STOP" TILE
// This board is a display terminal -- it cannot cut power to anything, so a
// Power Off button could only ever lie. A STOP tile was tried and removed too:
// nobody is standing on the Home screen while a test runs, so it earned its
// space far less than a status panel does. Every test screen has its own STOP.
static const int SW    = 224;                                   // status panel
static const int GW    = SCREEN_W - 2 * PAD_EDGE - SW - PAD_GAP;
static const int COLW  = (GW - 2 * PAD_GAP) / 3;
static const int TILEH = (SCREEN_H - CONTENT_Y - PAD_EDGE - PAD_GAP) / 2;

static int col_x(int c) { return PAD_EDGE + c * (COLW + PAD_GAP); }
static int row_y(int r) { return CONTENT_Y + r * (TILEH + PAD_GAP); }

// ---- Widgets updated in refresh() -----------------------------------------
static lv_obj_t* s_force   = nullptr;
static lv_obj_t* s_force_n = nullptr;
static lv_obj_t* s_chip    = nullptr;
static lv_obj_t* s_chip_l  = nullptr;
static lv_obj_t* s_kv[4]   = { nullptr };
static lv_obj_t* s_last    = nullptr;

// Set by the test screens when a run finishes, so Home can show what happened.
// Deliberately RAM-only: it resets on reboot, which is honest, and it avoids a
// flash write after every single run.
static char s_last_test[48] = "";
void home_set_last_test(const char* txt) {
    snprintf(s_last_test, sizeof(s_last_test), "%s", txt ? txt : "");
}

static void tile_cb(lv_event_t* e) {
    ui_goto((ScreenId)(intptr_t)lv_event_get_user_data(e));
}

static void make_tile(lv_obj_t* scr, int c, int r,
                      const char* sym, const char* text, ScreenId target) {
    lv_obj_t* b = w_button(scr, "", WBTN_PRIMARY, FONT_MD,
                           tile_cb, (void*)(intptr_t)target);
    lv_obj_set_size(b, COLW, TILEH);
    lv_obj_set_pos(b, col_x(c), row_y(r));
    lv_obj_set_style_radius(b, RADIUS_CARD, 0);

    lv_obj_t* ic = lv_obj_get_child(b, 0);      // the empty label w_button made
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, FONT_XL, 0);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 55);

    lv_obj_t* l = theme_label(b, text, FONT_MD, COL_TEXT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -16);
}

lv_obj_t* screen_home_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Home", false);   // hub screen: no Home button on itself

    // About now lives inside Settings, which frees the sixth tile for the new
    // Dynamic Loading screen and keeps the grid exactly full.
    make_tile(scr, 0, 0, LV_SYMBOL_DOWNLOAD, "Static\nTesting",  SCREEN_STATIC);
    make_tile(scr, 1, 0, LV_SYMBOL_LIST,     "Stepped\nLoading", SCREEN_STEPPED);
    make_tile(scr, 2, 0, LV_SYMBOL_LOOP,     "Dynamic\nLoading", SCREEN_DYNAMIC);
    make_tile(scr, 0, 1, LV_SYMBOL_GPS,      "Calibration",      SCREEN_CALIB);
    make_tile(scr, 1, 1, LV_SYMBOL_CHARGE,   "Diagnosis",        SCREEN_DIAG);
    make_tile(scr, 2, 1, LV_SYMBOL_SETTINGS, "Settings",         SCREEN_SETTINGS);

    // ---- Live status panel, full height ---------------------------------
    const int sx = SCREEN_W - PAD_EDGE - SW;
    lv_obj_t* st = theme_card(scr);
    lv_obj_set_size(st, SW, SCREEN_H - CONTENT_Y - PAD_EDGE);
    lv_obj_set_pos(st, sx, CONTENT_Y);
    lv_obj_set_style_pad_all(st, 18, 0);

    const int iw = SW - 36;
    lv_obj_align(w_caption(st, "RIG STATUS"), LV_ALIGN_TOP_LEFT, 0, 0);

    s_force = theme_label(st, "--", FONT_XL, COL_TEXT);
    lv_obj_align(s_force, LV_ALIGN_TOP_MID, -14, 30);
    lv_obj_align(theme_label(st, "kg", FONT_MD, COL_BLUE), LV_ALIGN_TOP_RIGHT, -4, 44);

    s_force_n = theme_label(st, "", FONT_XS, COL_MUTED);
    lv_obj_align(s_force_n, LV_ALIGN_TOP_MID, 0, 78);

    lv_obj_align(theme_separator(st, iw), LV_ALIGN_TOP_LEFT, 0, 102);

    // State chip
    s_chip = lv_obj_create(st);
    lv_obj_set_size(s_chip, iw, 38);
    lv_obj_set_style_radius(s_chip, 19, 0);
    lv_obj_set_style_bg_color(s_chip, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chip, 1, 0);
    lv_obj_clear_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_chip, LV_ALIGN_TOP_LEFT, 0, 116);
    s_chip_l = theme_label(s_chip, "Connecting", FONT_MD, COL_MUTED);
    lv_obj_center(s_chip_l);

    static const char* const KEYS[4] = { "Link", "Setpoint", "Limits", "Uptime" };
    for (int i = 0; i < 4; i++) {
        lv_obj_align(theme_label(st, KEYS[i], FONT_XS, COL_MUTED),
                     LV_ALIGN_TOP_LEFT, 0, 170 + i * 28);
        s_kv[i] = theme_label(st, "--", FONT_XS, COL_TEXT);
        lv_obj_align(s_kv[i], LV_ALIGN_TOP_RIGHT, 0, 170 + i * 28);
    }

    lv_obj_align(theme_separator(st, iw), LV_ALIGN_TOP_LEFT, 0, 288);
    lv_obj_align(w_caption(st, "LAST TEST"), LV_ALIGN_TOP_LEFT, 0, 300);
    s_last = theme_label(st, "none yet", FONT_XS, COL_TEXT);
    lv_obj_set_width(s_last, iw);
    lv_label_set_long_mode(s_last, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_last, LV_ALIGN_TOP_LEFT, 0, 320);

    return scr;
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz).  Reads g_app only; never sends anything.
// ---------------------------------------------------------------------------
void screen_home_refresh() {
    if (!s_force) return;

    char b[40];

    // ---- Big force readout ----------------------------------------------
    if (g_app.link_ok) {
        snprintf(b, sizeof(b), "%.2f", g_app.force_kg);
        lv_label_set_text(s_force, b);
        lv_obj_set_style_text_color(s_force, COL_TEXT, 0);
        snprintf(b, sizeof(b), "%.1f N", kg_to_n(g_app.force_kg));
        lv_label_set_text(s_force_n, b);
    } else {
        // No link: dashes, never the last number we happened to hear.
        lv_label_set_text(s_force, "--");
        lv_obj_set_style_text_color(s_force, COL_FAINT, 0);
        lv_label_set_text(s_force_n, "");
    }

    // ---- State chip -------------------------------------------------------
    const char* txt; lv_color_t col;
    if (!g_app.link_ok)                         { txt = "No link";  col = COL_ORANGE; }
    else switch (g_app.rig) {
        case RigState::Fault:   txt = "FAULT";   col = COL_RED;    break;
        case RigState::Running: txt = "Running"; col = COL_ORANGE; break;
        case RigState::Idle:    txt = "Ready";   col = COL_GREEN;  break;
        default:                txt = "Waiting"; col = COL_MUTED;  break;
    }
    lv_label_set_text(s_chip_l, txt);
    lv_obj_set_style_text_color(s_chip_l, col, 0);
    lv_obj_set_style_border_color(s_chip, col, 0);

    // ---- Key / value rows -------------------------------------------------
    lv_label_set_text(s_kv[0], g_app.link_ok ? "Connected" : "Offline");
    lv_obj_set_style_text_color(s_kv[0], g_app.link_ok ? COL_GREEN : COL_ORANGE, 0);

    if (g_app.link_ok) snprintf(b, sizeof(b), "%.1f kg", g_app.target_kg);
    else               snprintf(b, sizeof(b), "--");
    lv_label_set_text(s_kv[1], b);

    const bool ph = (g_app.limits & 1), rh = (g_app.limits & 2);
    lv_label_set_text(s_kv[2], !g_app.link_ok ? "--" :
                      (ph && rh) ? "BOTH?" : ph ? "Push hit" : rh ? "Retract hit" : "Clear");

    const uint32_t up = millis() / 1000;
    snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)(up / 3600),
             (unsigned long)((up / 60) % 60), (unsigned long)(up % 60));
    lv_label_set_text(s_kv[3], b);

    lv_label_set_text(s_last, s_last_test[0] ? s_last_test : "none yet");
}
