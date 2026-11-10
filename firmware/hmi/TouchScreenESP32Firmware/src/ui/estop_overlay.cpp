#include "estop_overlay.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <Arduino.h>
#include <stdio.h>

// ---- Layout (matches the approved render) ---------------------------------
static const int CARD_W  = 580;
static const int CARD_H  = 388;
static const int CARD_X  = (SCREEN_W - CARD_W) / 2;     // 110
static const int CARD_Y  = TOPBAR_H + 18;               // 74
static const int INSET_W = CARD_W - 48;                 // 532
static const int BTN_Y   = 290;
static const int BTN_H   = 72;

// ---- Widgets --------------------------------------------------------------
static lv_obj_t* s_bg        = nullptr;   // dim + touch blocker, owns everything
static lv_obj_t* s_body      = nullptr;
static WChip     s_chip;
static lv_obj_t* s_force     = nullptr;
static lv_obj_t* s_on_reset  = nullptr;
static lv_obj_t* s_btn_wait  = nullptr;   // shown while pressed
static lv_obj_t* s_btn_later = nullptr;   // shown once released
static lv_obj_t* s_btn_reset = nullptr;

// ---- State ----------------------------------------------------------------
static bool     s_last_estop   = false;   // for rising-edge detection
static bool     s_dismissed    = false;   // "Not now" pressed for this e-stop
static uint32_t s_reset_sent_ms = 0;      // debounce the Reset button

bool estop_overlay_is_open() { return s_bg != nullptr; }

static void overlay_close() {
    if (!s_bg) return;
    lv_obj_del(s_bg);
    s_bg = nullptr;
    s_body = s_force = s_on_reset = nullptr;
    s_btn_wait = s_btn_later = s_btn_reset = nullptr;
    s_chip = WChip{};
}

static void later_cb(lv_event_t* /*e*/) {
    s_dismissed = true;
    overlay_close();
    w_toast("Rig still faulted - reset before the next test", COL_ORANGE, 3000);
}

static void reset_cb(lv_event_t* /*e*/) {
    if (g_app.estop) return;                       // belt and braces
    link_cmd_reset();
    s_reset_sent_ms = millis();
    w_toast(g_settings.retract_after_estop ? "Reset sent - rig will retract 5 mm"
                                           : "Reset sent",
            COL_BLUE, 2500);
}

// Small helper: absolutely positioned label.
static lv_obj_t* label_at(lv_obj_t* p, const char* t, const lv_font_t* f,
                          lv_color_t c, int x, int y) {
    lv_obj_t* l = theme_label(p, t, f, c);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t* hline(lv_obj_t* p, int x, int y, int w) {
    lv_obj_t* s = theme_separator(p, w);
    lv_obj_set_pos(s, x, y);
    return s;
}

static void overlay_open() {
    overlay_close();
    w_modal_close();               // the generic fault popup must not sit on top

    // ---- Backdrop: covers everything below the top bar, so the e-stop
    // badge in the bar stays visible and Home still works.
    s_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bg, SCREEN_W, SCREEN_H - TOPBAR_H);
    lv_obj_set_pos(s_bg, 0, TOPBAR_H);
    lv_obj_set_style_bg_color(s_bg, lv_color_hex(0x030508), 0);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_bg, 0, 0);
    lv_obj_set_style_radius(s_bg, 0, 0);
    lv_obj_set_style_pad_all(s_bg, 0, 0);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bg, LV_OBJ_FLAG_CLICKABLE);

    // ---- Card ---------------------------------------------------------------
    lv_obj_t* card = theme_card(s_bg);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, CARD_X, CARD_Y - TOPBAR_H);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_color(card, COL_RED, 0);
    lv_obj_set_style_border_width(card, 2, 0);

    // Icon: red tile with a white "!" (the built-in fonts have no octagon).
    lv_obj_t* icon = lv_obj_create(card);
    lv_obj_set_size(icon, 54, 54);
    lv_obj_set_pos(icon, 24, 22);
    lv_obj_set_style_bg_color(icon, COL_RED, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icon, 14, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(theme_label(icon, "!", FONT_XL, COL_TEXT));

    lv_obj_t* title = label_at(card, "EMERGENCY STOP", FONT_LG, COL_RED, 94, 26);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    s_body = label_at(card, "", FONT_SM, COL_MUTED, 94, 62);
    lv_obj_set_width(s_body, CARD_W - 94 - 24);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);

    // ---- Status inset ---------------------------------------------------------
    lv_obj_t* in = lv_obj_create(card);
    lv_obj_set_size(in, INSET_W, 150);
    lv_obj_set_pos(in, 24, 116);
    lv_obj_set_style_bg_color(in, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(in, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(in, 10, 0);
    lv_obj_set_style_border_width(in, 0, 0);
    lv_obj_set_style_pad_all(in, 0, 0);
    lv_obj_clear_flag(in, LV_OBJ_FLAG_SCROLLABLE);

    label_at(in, "E-stop switch", FONT_SM, COL_MUTED, 18, 16);
    s_chip = w_chip(in, "PRESSED", COL_RED, 132);
    lv_obj_align(s_chip.cont, LV_ALIGN_TOP_RIGHT, -14, 10);
    hline(in, 18, 50, INSET_W - 36);

    label_at(in, "Force on specimen", FONT_SM, COL_MUTED, 18, 62);
    s_force = theme_label(in, "--", FONT_SM, COL_ORANGE);
    lv_obj_align(s_force, LV_ALIGN_TOP_RIGHT, -18, 62);
    label_at(in, "The leadscrew holds position, so the load is still applied.",
             FONT_XS, COL_FAINT, 18, 86);
    hline(in, 18, 112, INSET_W - 36);

    label_at(in, "On reset", FONT_SM, COL_MUTED, 18, 122);
    s_on_reset = theme_label(in, "--", FONT_SM, COL_TEXT);
    lv_obj_align(s_on_reset, LV_ALIGN_TOP_RIGHT, -18, 122);

    // ---- Buttons ----------------------------------------------------------
    // While pressed: one greyed full-width button that says what to do.
    s_btn_wait = w_button(card, "Reset unavailable\nRelease the e-stop switch first",
                          WBTN_SECONDARY, FONT_SM, nullptr, nullptr);
    lv_obj_set_size(s_btn_wait, INSET_W, BTN_H);
    lv_obj_set_pos(s_btn_wait, 24, BTN_Y);
    lv_obj_set_style_text_align(lv_obj_get_child(s_btn_wait, 0), LV_TEXT_ALIGN_CENTER, 0);
    w_button_enable(s_btn_wait, false);

    // Once released: Not now + Reset.
    s_btn_later = w_button(card, "Not now", WBTN_GHOST, FONT_MD, later_cb, nullptr);
    lv_obj_set_size(s_btn_later, 150, BTN_H);
    lv_obj_set_pos(s_btn_later, 24, BTN_Y);

    s_btn_reset = w_button(card, "", WBTN_PRIMARY, FONT_MD, reset_cb, nullptr);
    lv_obj_set_size(s_btn_reset, INSET_W - 150 - 12, BTN_H);
    lv_obj_set_pos(s_btn_reset, 24 + 150 + 12, BTN_Y);
}

// Repaint the dynamic parts. Cheap enough to run every tick.
static void overlay_paint() {
    if (!s_bg) return;
    const bool pressed = g_app.estop;

    lv_label_set_text(s_body, pressed
        ? "Motor power is cut at the switch. Any running test was stopped "
          "and its log file closed."
        : "The switch is released and motor power is back. Reset the rig "
          "to restore motion.");

    if (pressed) {
        // Blink the chip so "still pressed" reads at a glance.
        w_chip_set(s_chip, "PRESSED", COL_RED);
        lv_obj_set_style_bg_color(s_chip.cont, COL_RED_BG, 0);
        lv_obj_set_style_opa(s_chip.cont, ((millis() / 400) % 2) ? LV_OPA_COVER : LV_OPA_60, 0);
    } else {
        w_chip_set(s_chip, "RELEASED", COL_GREEN);
        lv_obj_set_style_bg_color(s_chip.cont, COL_GREEN_BG, 0);
        lv_obj_set_style_opa(s_chip.cont, LV_OPA_COVER, 0);
    }

    char f[16], b[32];
    snprintf(b, sizeof(b), "%s %s", fmt_force(g_app.force_kg, f, sizeof(f)), unit_str());
    lv_label_set_text(s_force, b);
    lv_obj_align(s_force, LV_ALIGN_TOP_RIGHT, -18, 62);

    lv_label_set_text(s_on_reset, g_settings.retract_after_estop ? "Retract 5 mm" : "Clear fault only");
    lv_obj_align(s_on_reset, LV_ALIGN_TOP_RIGHT, -18, 122);

    if (pressed) {
        lv_obj_clear_flag(s_btn_wait,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_btn_later, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_btn_reset, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag  (s_btn_wait,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_later, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_reset, LV_OBJ_FLAG_HIDDEN);

        // Grey Reset out for 3 s after a press, so one tap sends one 'Z'.
        const bool busy = (s_reset_sent_ms != 0) && (millis() - s_reset_sent_ms < 3000);
        w_button_set_text(s_btn_reset, busy ? "Resetting..."
                          : (g_settings.retract_after_estop ? LV_SYMBOL_REFRESH "  Reset & retract 5 mm"
                                                            : LV_SYMBOL_REFRESH "  Reset rig"));
        w_button_enable(s_btn_reset, !busy);
    }

    // Stay above any keypad or dialog opened after us.
    lv_obj_move_foreground(s_bg);
}

void estop_overlay_update() {
    const bool es = g_app.link_ok && g_app.estop;

    // A new press always re-opens the panel, even after "Not now".
    if (es && !s_last_estop) {
        s_dismissed     = false;
        s_reset_sent_ms = 0;
    }
    s_last_estop = es;

    if (!g_app.link_ok) { overlay_close(); return; }

    if (es && !s_bg) overlay_open();

    // Done once the rig is out of the fault and the switch is closed.
    if (s_bg && !es && g_app.rig != RigState::Fault) {
        overlay_close();
        s_dismissed = false;
        w_toast(LV_SYMBOL_OK "  Rig reset", COL_GREEN, 2000);
        return;
    }

    if (s_bg && s_dismissed) { overlay_close(); return; }

    overlay_paint();
}
