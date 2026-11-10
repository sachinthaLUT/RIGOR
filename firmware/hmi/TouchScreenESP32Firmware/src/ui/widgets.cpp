#include "widgets.h"
#include <string.h>
#include <stdlib.h>   // atof, for the keypad
#include <stdio.h>    // snprintf

// ===========================================================================
//  BUTTONS
// ===========================================================================

// Apply the colour scheme for a style. Split out so w_button_set_style()
// can re-apply it later without rebuilding the button.
static void apply_btn_style(lv_obj_t* b, WBtnStyle style) {
    lv_obj_set_style_radius(b, RADIUS_CTRL, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);   // flat: shadows cost fill rate
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);

    // ZERO THE PADDING.  This matters more than it looks.
    //
    // LVGL's default theme gives lv_btn a generous internal pad (roughly 15 px
    // top/bottom at this font scale). Every lv_obj_align() on a CHILD is
    // measured against the button's CONTENT box, not its outer size -- so on a
    // 76 px card the usable box was only ~46 px, and a caption placed at
    // TOP_LEFT +10 landed on top of a value placed at BOTTOM_LEFT -10.
    //
    // Zeroing it here means offsets mean what they say, everywhere, for every
    // caller. Centred single labels (the common case) are unaffected.
    //
    // Any button that WANTS padding sets it after the w_button() call --
    // e.g. the top bar's Home pill uses pad_hor for content-sizing.
    lv_obj_set_style_pad_all(b, 0, 0);

    switch (style) {
        case WBTN_PRIMARY:
            lv_obj_set_style_bg_color(b, COL_BLUE, 0);
            lv_obj_set_style_bg_color(b, COL_BLUE_DK, LV_STATE_PRESSED);
            break;
        case WBTN_SECONDARY:
            lv_obj_set_style_bg_color(b, COL_CARD2, 0);
            lv_obj_set_style_bg_color(b, COL_CARD3, LV_STATE_PRESSED);
            break;
        case WBTN_SUCCESS:
            lv_obj_set_style_bg_color(b, COL_GREEN, 0);
            lv_obj_set_style_bg_color(b, COL_GREEN_DK, LV_STATE_PRESSED);
            break;
        case WBTN_DANGER:
            lv_obj_set_style_bg_color(b, COL_RED_BG, 0);
            lv_obj_set_style_bg_color(b, COL_RED_BD, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(b, COL_RED, 0);
            lv_obj_set_style_border_width(b, 2, 0);
            break;
        case WBTN_GHOST:
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_bg_color(b, COL_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(b, COL_TRACK, 0);
            lv_obj_set_style_border_width(b, 2, 0);
            break;
    }
}

// Text colour that reads well on each style.
static lv_color_t btn_text_color(WBtnStyle style) {
    switch (style) {
        case WBTN_DANGER: return COL_RED;
        case WBTN_GHOST:  return COL_MUTED;
        default:          return COL_TEXT;
    }
}

lv_obj_t* w_button(lv_obj_t* parent, const char* txt, WBtnStyle style,
                   const lv_font_t* font, lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = lv_btn_create(parent);
    apply_btn_style(b, style);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t* l = theme_label(b, txt, font ? font : FONT_MD, btn_text_color(style));
    lv_obj_center(l);
    return b;
}

// Helper: the label is always child 0 of a w_button().
static lv_obj_t* btn_label(lv_obj_t* btn) {
    return lv_obj_get_child_cnt(btn) > 0 ? lv_obj_get_child(btn, 0) : nullptr;
}

void w_button_set_text(lv_obj_t* btn, const char* txt) {
    lv_obj_t* l = btn_label(btn);
    if (l) lv_label_set_text(l, txt);
}

void w_button_set_style(lv_obj_t* btn, WBtnStyle style) {
    apply_btn_style(btn, style);
    lv_obj_t* l = btn_label(btn);
    if (l) lv_obj_set_style_text_color(l, btn_text_color(style), 0);
}

void w_button_enable(lv_obj_t* btn, bool enabled) {
    if (enabled) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(btn, LV_OPA_50, 0);   // visibly greyed out
    }
}

// ===========================================================================
//  STEPPER
// ===========================================================================
lv_obj_t* w_stepper(lv_obj_t* parent, int width, int height,
                    lv_event_cb_t on_minus, lv_event_cb_t on_plus,
                    void* ud, lv_obj_t** out_value_label) {
    // Transparent container so the stepper can sit on a card or a screen.
    lv_obj_t* w = lv_obj_create(parent);
    lv_obj_set_size(w, width, height);
    lv_obj_set_style_bg_opa(w, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w, 0, 0);
    lv_obj_set_style_pad_all(w, 0, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);

    // Square -/+ buttons at each end, value centred between them.
    lv_obj_t* m = w_button(w, LV_SYMBOL_MINUS, WBTN_SECONDARY, FONT_MD, on_minus, ud);
    lv_obj_set_size(m, height, height);
    lv_obj_align(m, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* p = w_button(w, LV_SYMBOL_PLUS, WBTN_SECONDARY, FONT_MD, on_plus, ud);
    lv_obj_set_size(p, height, height);
    lv_obj_align(p, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* v = theme_label(w, "--", FONT_MD, COL_TEXT);
    lv_obj_center(v);

    if (out_value_label) *out_value_label = v;
    return w;
}

// ===========================================================================
//  BIG VALUE READOUT
// ===========================================================================
WReadout w_readout(lv_obj_t* parent, int width, int height,
                   const lv_font_t* value_font) {
    WReadout r;

    r.cont = lv_obj_create(parent);
    lv_obj_set_size(r.cont, width, height);
    lv_obj_set_style_bg_opa(r.cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r.cont, 0, 0);
    lv_obj_set_style_pad_all(r.cont, 0, 0);
    lv_obj_clear_flag(r.cont, LV_OBJ_FLAG_SCROLLABLE);

    // The big number sits slightly LEFT of centre so the unit next to it
    // keeps the pair visually centred.
    r.value = theme_label(r.cont, "--", value_font ? value_font : FONT_HUGE, COL_TEXT);
    lv_obj_align(r.value, LV_ALIGN_CENTER, -16, -8);

    r.unit = theme_label(r.cont, "kg", FONT_LG, COL_BLUE);
    lv_obj_align_to(r.unit, r.value, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);

    r.sub = theme_label(r.cont, "", FONT_SM, COL_MUTED);
    lv_obj_align(r.sub, LV_ALIGN_BOTTOM_MID, 0, 0);

    return r;
}

void w_readout_set(WReadout& r, const char* value_txt,
                   const char* unit_txt, const char* sub_txt) {
    if (value_txt && r.value) {
        lv_label_set_text(r.value, value_txt);
        // Re-anchor the unit: the number's width changes with its digits.
        if (r.unit) lv_obj_align_to(r.unit, r.value, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);
    }
    if (unit_txt && r.unit) {
        lv_label_set_text(r.unit, unit_txt);
        lv_obj_align_to(r.unit, r.value, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);
    }
    if (sub_txt && r.sub) lv_label_set_text(r.sub, sub_txt);
}

void w_readout_set_color(WReadout& r, lv_color_t value_col, lv_color_t unit_col) {
    if (r.value) lv_obj_set_style_text_color(r.value, value_col, 0);
    if (r.unit)  lv_obj_set_style_text_color(r.unit,  unit_col,  0);
}

// ===========================================================================
//  STATUS CHIP
// ===========================================================================
WChip w_chip(lv_obj_t* parent, const char* txt, lv_color_t col, int width) {
    WChip c;

    c.cont = lv_obj_create(parent);
    lv_obj_set_size(c.cont, width, 32);
    lv_obj_set_style_bg_color(c.cont, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(c.cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c.cont, 16, 0);
    lv_obj_set_style_border_width(c.cont, 0, 0);
    lv_obj_set_style_pad_all(c.cont, 0, 0);
    lv_obj_clear_flag(c.cont, LV_OBJ_FLAG_SCROLLABLE);

    // Coloured dot: a tiny circle = an object with radius >= half its size.
    c.dot = lv_obj_create(c.cont);
    lv_obj_set_size(c.dot, 10, 10);
    lv_obj_set_style_radius(c.dot, 5, 0);
    lv_obj_set_style_bg_color(c.dot, col, 0);
    lv_obj_set_style_bg_opa(c.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c.dot, 0, 0);
    lv_obj_clear_flag(c.dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(c.dot, LV_ALIGN_LEFT_MID, 12, 0);

    c.lbl = theme_label(c.cont, txt, FONT_XS, col);
    lv_obj_align(c.lbl, LV_ALIGN_LEFT_MID, 30, 0);

    return c;
}

void w_chip_set(WChip& c, const char* txt, lv_color_t col) {
    if (c.dot) lv_obj_set_style_bg_color(c.dot, col, 0);
    if (c.lbl) {
        lv_label_set_text(c.lbl, txt);
        lv_obj_set_style_text_color(c.lbl, col, 0);
    }
}

// ===========================================================================
//  KEY / VALUE ROW
// ===========================================================================
lv_obj_t* w_kv_row(lv_obj_t* card, const char* key, int y, int row_width) {
    (void)row_width;   // kept in the signature: handy if you ever switch to
                       // absolute positioning instead of card-relative align

    lv_obj_t* k = theme_label(card, key, FONT_SM, COL_MUTED);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y);

    // Value hugs the card's right edge, so rows line up regardless of the
    // key's length.
    lv_obj_t* v = theme_label(card, "--", FONT_SM, COL_TEXT);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    return v;
}

// ===========================================================================
//  SECTION CAPTION
// ===========================================================================
lv_obj_t* w_caption(lv_obj_t* parent, const char* txt) {
    lv_obj_t* l = theme_label(parent, txt, FONT_XS, COL_MUTED);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    return l;
}

// ===========================================================================
//  STAGE INDICATOR
// ===========================================================================
WStages w_stages(lv_obj_t* parent, int width, const char* const* names, int count) {
    WStages s;
    if (count > W_STAGE_MAX) count = W_STAGE_MAX;
    s.count = count;

    const int seg = width / count;     // horizontal slot per stage

    for (int i = 0; i < count; i++) {
        const int cx = seg * i + seg / 2;   // centre of this stage's slot

        // Connector line to the NEXT stage, drawn first so dots sit on top.
        if (i < count - 1) {
            lv_obj_t* ln = lv_obj_create(parent);
            lv_obj_set_size(ln, seg - 30, 2);
            lv_obj_set_style_bg_color(ln, COL_TRACK, 0);
            lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ln, 0, 0);
            lv_obj_set_style_pad_all(ln, 0, 0);
            lv_obj_clear_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(ln, LV_ALIGN_TOP_LEFT, cx + 15, 10);
        }

        // The dot itself.
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_size(d, 22, 22);
        lv_obj_set_style_radius(d, 11, 0);
        lv_obj_set_style_bg_color(d, COL_CARD2, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(d, COL_TRACK, 0);
        lv_obj_set_style_border_width(d, 2, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, cx - 11, 0);
        s.dot[i] = d;

        lv_obj_t* l = theme_label(parent, names[i], FONT_XS, COL_FAINT);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, seg);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, seg * i, 30);
        s.label[i] = l;
    }
    return s;
}

void w_stages_set(WStages& s, int active) {
    for (int i = 0; i < s.count; i++) {
        if (!s.dot[i]) continue;

        if (i < active) {
            // Completed: filled green.
            lv_obj_set_style_bg_color(s.dot[i], COL_GREEN, 0);
            lv_obj_set_style_border_color(s.dot[i], COL_GREEN, 0);
            lv_obj_set_style_text_color(s.label[i], COL_MUTED, 0);
        } else if (i == active) {
            // Current: filled amber, label bright.
            lv_obj_set_style_bg_color(s.dot[i], COL_ORANGE, 0);
            lv_obj_set_style_border_color(s.dot[i], COL_ORANGE, 0);
            lv_obj_set_style_text_color(s.label[i], COL_TEXT, 0);
        } else {
            // Pending: hollow.
            lv_obj_set_style_bg_color(s.dot[i], COL_CARD2, 0);
            lv_obj_set_style_border_color(s.dot[i], COL_TRACK, 0);
            lv_obj_set_style_text_color(s.label[i], COL_FAINT, 0);
        }
    }
}

// ===========================================================================
//  MODAL DIALOG
//
//  Built on lv_layer_top(): that layer is drawn above the active screen and
//  is NOT destroyed by lv_scr_load(), so a fault popup stays visible even if
//  the screen changes underneath it.
// ===========================================================================
static lv_obj_t*    s_modal_bg = nullptr;   // full-screen dim + input blocker
static w_modal_cb_t s_modal_ok = nullptr;

bool w_modal_is_open() { return s_modal_bg != nullptr; }

void w_modal_close() {
    if (!s_modal_bg) return;
    lv_obj_del(s_modal_bg);
    s_modal_bg = nullptr;
    s_modal_ok = nullptr;
}

static void modal_ok_cb(lv_event_t* /*e*/) {
    // Copy the callback out BEFORE closing: w_modal_close() clears it, and
    // the handler may well open another modal.
    w_modal_cb_t cb = s_modal_ok;
    w_modal_close();
    if (cb) cb();
}

static void modal_cancel_cb(lv_event_t* /*e*/) {
    w_modal_close();
}

void w_modal(const char* title, const char* msg, lv_color_t accent,
             const char* ok_txt, const char* cancel_txt, w_modal_cb_t on_ok) {
    w_modal_close();          // only one at a time
    s_modal_ok = on_ok;

    // --- Dim backdrop. CLICKABLE so touches can't reach the screen below.
    s_modal_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_modal_bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_modal_bg, 0, 0);
    lv_obj_set_style_bg_color(s_modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal_bg, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_modal_bg, 0, 0);
    lv_obj_set_style_radius(s_modal_bg, 0, 0);
    lv_obj_set_style_pad_all(s_modal_bg, 0, 0);
    lv_obj_clear_flag(s_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal_bg, LV_OBJ_FLAG_CLICKABLE);

    // --- Dialog box -------------------------------------------------------
    const int DW = 520;                       // dialog width  -- tweak freely
    const int DH = cancel_txt ? 260 : 240;    // taller when two buttons

    lv_obj_t* box = theme_card(s_modal_bg);
    lv_obj_set_size(box, DW, DH);
    lv_obj_center(box);
    lv_obj_set_style_border_color(box, accent, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_pad_all(box, 24, 0);

    lv_obj_t* t = theme_label(box, title, FONT_LG, accent);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    // Body text wraps inside the box width.
    lv_obj_t* m = theme_label(box, msg, FONT_MD, COL_MUTED);
    lv_obj_set_width(m, DW - 48);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 56);

    // --- Buttons ----------------------------------------------------------
    const int BW = cancel_txt ? (DW - 48 - PAD_GAP) / 2 : (DW - 48);
    const int BH = 56;

    if (cancel_txt) {
        lv_obj_t* c = w_button(box, cancel_txt, WBTN_SECONDARY, FONT_MD,
                               modal_cancel_cb, nullptr);
        lv_obj_set_size(c, BW, BH);
        lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    if (ok_txt) {
        // A red accent means this is a fault/destructive dialog.
        WBtnStyle st = (accent.full == COL_RED.full) ? WBTN_DANGER : WBTN_PRIMARY;
        lv_obj_t* o = w_button(box, ok_txt, st, FONT_MD, modal_ok_cb, nullptr);
        lv_obj_set_size(o, BW, BH);
        lv_obj_align(o, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
}

// ===========================================================================
//  TOAST
// ===========================================================================
static lv_obj_t* s_toast = nullptr;

static void toast_timer_cb(lv_timer_t* t) {
    if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; }
    lv_timer_del(t);
}

void w_toast(const char* txt, lv_color_t accent, uint32_t ms) {
    if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; }

    // On layer_top like the modal, but NOT clickable: it must not block the UI.
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_bg_color(s_toast, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 24, 0);
    lv_obj_set_style_border_color(s_toast, accent, 0);
    lv_obj_set_style_border_width(s_toast, 2, 0);
    lv_obj_set_style_pad_hor(s_toast, 24, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_center(theme_label(s_toast, txt, FONT_SM, accent));

    lv_timer_t* t = lv_timer_create(toast_timer_cb, ms, nullptr);
    lv_timer_set_repeat_count(t, 1);
}

// ===========================================================================
//  NUMERIC KEYPAD  (modal)
//
//  Same lv_layer_top() approach as w_modal(): floats above the active screen,
//  blocks touches underneath, and only one can exist at a time.
//
//  The entered value is kept as a STRING while typing, not a float. Typing
//  "3", then ".", then "5" has no valid float representation at the middle
//  step, and round-tripping through a float would delete the decimal point
//  the moment it was pressed.
// ===========================================================================
static lv_obj_t*     s_kp_bg    = nullptr;   // backdrop (owns everything)
static lv_obj_t*     s_kp_value = nullptr;   // the value label
static lv_obj_t*     s_kp_set   = nullptr;   // SET button (enabled/disabled)
static lv_obj_t*     s_kp_hint  = nullptr;   // range hint under the field
static w_keypad_cb_t s_kp_cb    = nullptr;
static char          s_kp_buf[12] = "";
static float         s_kp_min = 0, s_kp_max = 0;
static int           s_kp_decimals = 0;

static void kp_close() {
    if (!s_kp_bg) return;
    lv_obj_del(s_kp_bg);
    s_kp_bg    = nullptr;
    s_kp_value = nullptr;
    s_kp_set   = nullptr;
    s_kp_hint  = nullptr;
    s_kp_cb    = nullptr;
}

// Empty string is NOT zero: it means "nothing typed yet", which must not pass
// validation just because 0 happens to be within range.
static bool kp_valid(float* out) {
    if (s_kp_buf[0] == '\0') return false;
    if (s_kp_buf[0] == '.' && s_kp_buf[1] == '\0') return false;
    float v = atof(s_kp_buf);
    if (out) *out = v;
    return (v >= s_kp_min && v <= s_kp_max);
}

// Repaint the value, and enable/disable SET to match validity.
static void kp_sync() {
    if (!s_kp_value) return;
    lv_label_set_text(s_kp_value, s_kp_buf[0] ? s_kp_buf : "0");

    float v = 0;
    const bool ok = kp_valid(&v);
    w_button_enable(s_kp_set, ok);

    // Explain WHY set is unavailable, rather than just greying it out.
    if (!ok && s_kp_buf[0] != '\0') {
        char h[48];
        snprintf(h, sizeof(h), "Must be %.0f - %.0f", s_kp_min, s_kp_max);
        lv_label_set_text(s_kp_hint, h);
        lv_obj_set_style_text_color(s_kp_hint, COL_ORANGE, 0);
        lv_obj_set_style_text_color(s_kp_value, COL_ORANGE, 0);
    } else {
        char h[48];
        snprintf(h, sizeof(h), "Maximum %.0f", s_kp_max);
        lv_label_set_text(s_kp_hint, h);
        lv_obj_set_style_text_color(s_kp_hint, COL_FAINT, 0);
        lv_obj_set_style_text_color(s_kp_value, COL_TEXT, 0);
    }
}

static void kp_key_cb(lv_event_t* e) {
    const char k = (char)(intptr_t)lv_event_get_user_data(e);
    size_t len = strlen(s_kp_buf);

    if (k == '<') {                                  // backspace
        if (len > 0) s_kp_buf[len - 1] = '\0';
    } else if (k == '.') {
        if (s_kp_decimals > 0 && !strchr(s_kp_buf, '.') && len < sizeof(s_kp_buf) - 2) {
            s_kp_buf[len] = '.';
            s_kp_buf[len + 1] = '\0';
        }
    } else {                                         // a digit
        // Cap the digits after the decimal point at s_kp_decimals.
        const char* dot = strchr(s_kp_buf, '.');
        if (dot && (int)strlen(dot + 1) >= s_kp_decimals) return;
        if (len < sizeof(s_kp_buf) - 2) {
            s_kp_buf[len] = k;
            s_kp_buf[len + 1] = '\0';
        }
    }
    kp_sync();
}

static void kp_cancel_cb(lv_event_t*) { kp_close(); }

static void kp_set_cb(lv_event_t*) {
    float v = 0;
    if (!kp_valid(&v)) return;              // belt and braces; SET is disabled
    w_keypad_cb_t cb = s_kp_cb;             // copy out before close clears it
    kp_close();
    if (cb) cb(v);
}

void w_keypad(const char* title, const char* unit,
              float initial, float min_val, float max_val,
              int decimals, w_keypad_cb_t on_set) {
    kp_close();
    w_modal_close();                        // never stack a keypad on a dialog

    s_kp_cb       = on_set;
    s_kp_min      = min_val;
    s_kp_max      = max_val;
    s_kp_decimals = decimals;

    if (initial > 0) {
        if (decimals > 0) snprintf(s_kp_buf, sizeof(s_kp_buf), "%.*f", decimals, initial);
        else              snprintf(s_kp_buf, sizeof(s_kp_buf), "%.0f", initial);
    } else {
        s_kp_buf[0] = '\0';
    }

    // ---- Backdrop ---------------------------------------------------------
    s_kp_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_kp_bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_kp_bg, 0, 0);
    lv_obj_set_style_bg_color(s_kp_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_kp_bg, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_kp_bg, 0, 0);
    lv_obj_set_style_radius(s_kp_bg, 0, 0);
    lv_obj_set_style_pad_all(s_kp_bg, 0, 0);
    lv_obj_clear_flag(s_kp_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kp_bg, LV_OBJ_FLAG_CLICKABLE);

    // ---- Dialog box -------------------------------------------------------
    const int DW = 372, DH = 428;
    lv_obj_t* box = theme_card(s_kp_bg);
    lv_obj_set_size(box, DW, DH);
    lv_obj_center(box);
    lv_obj_set_style_border_color(box, COL_BLUE, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_pad_all(box, 20, 0);

    const int IW = DW - 40;                 // inner width

    lv_obj_align(theme_label(box, title, FONT_MD, COL_TEXT), LV_ALIGN_TOP_MID, 0, 0);

    s_kp_hint = theme_label(box, "", FONT_XS, COL_FAINT);
    lv_obj_align(s_kp_hint, LV_ALIGN_TOP_MID, 0, 26);

    // ---- Value field ------------------------------------------------------
    lv_obj_t* field = lv_obj_create(box);
    lv_obj_set_size(field, IW, 56);
    lv_obj_set_style_bg_color(field, COL_CARD2, 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(field, RADIUS_CTRL, 0);
    lv_obj_set_style_border_color(field, COL_BLUE, 0);
    lv_obj_set_style_border_width(field, 2, 0);
    lv_obj_set_style_pad_all(field, 0, 0);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(field, LV_ALIGN_TOP_MID, 0, 48);

    s_kp_value = theme_label(field, "0", FONT_LG, COL_TEXT);
    lv_obj_align(s_kp_value, LV_ALIGN_CENTER, -18, 0);
    lv_obj_align(theme_label(field, unit, FONT_MD, COL_BLUE), LV_ALIGN_CENTER, 40, 2);

    // ---- Keypad grid ------------------------------------------------------
    static const char KEYS[4][3] = {
        { '1','2','3' }, { '4','5','6' }, { '7','8','9' }, { '.','0','<' }
    };
    const int KGAP = 7;
    const int KW = (IW - 2 * KGAP) / 3;
    const int KH = 44;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            const char k = KEYS[r][c];

            // Integer-only fields get a dead '.' key rather than a hole in
            // the grid -- a missing key looks like a rendering bug.
            const bool dead = (k == '.' && decimals == 0);

            char lbl[4] = { k, '\0', '\0', '\0' };
            const char* txt = (k == '<') ? LV_SYMBOL_BACKSPACE : lbl;

            lv_obj_t* b = w_button(box, txt, WBTN_SECONDARY, FONT_MD,
                                   dead ? nullptr : kp_key_cb,
                                   (void*)(intptr_t)k);
            lv_obj_set_size(b, KW, KH);
            lv_obj_align(b, LV_ALIGN_TOP_LEFT, c * (KW + KGAP), 118 + r * (KH + KGAP));
            lv_obj_set_style_radius(b, 9, 0);
            if (dead) w_button_enable(b, false);
        }
    }

    // ---- Cancel / Set -----------------------------------------------------
    const int BW = (IW - 10) / 2;
    lv_obj_t* c = w_button(box, "Cancel", WBTN_GHOST, FONT_MD, kp_cancel_cb, nullptr);
    lv_obj_set_size(c, BW, 48);
    lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_kp_set = w_button(box, "Set", WBTN_PRIMARY, FONT_MD, kp_set_cb, nullptr);
    lv_obj_set_size(s_kp_set, BW, 48);
    lv_obj_align(s_kp_set, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    kp_sync();
}

// ===========================================================================
//  TEXT KEYBOARD  (modal)
//
//  Uses LVGL's built-in lv_keyboard, which already handles layout switching
//  (letters / numbers / symbols) and is far less code than hand-rolling a
//  QWERTY. It is placed on lv_layer_top() like the other modals.
// ===========================================================================
static lv_obj_t*     s_kb_bg   = nullptr;
static lv_obj_t*     s_kb_ta   = nullptr;
static w_textkb_cb_t s_kb_cb   = nullptr;

static void textkb_close() {
    if (!s_kb_bg) return;
    lv_obj_del(s_kb_bg);
    s_kb_bg = nullptr;
    s_kb_ta = nullptr;
    s_kb_cb = nullptr;
}

static void textkb_event_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {                 // green tick on the keyboard
        // Copy BOTH the text and the callback out before closing: the close
        // frees the textarea and clears the callback pointer.
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", lv_textarea_get_text(s_kb_ta));
        w_textkb_cb_t cb = s_kb_cb;
        textkb_close();
        if (cb) cb(buf);
    } else if (code == LV_EVENT_CANCEL) {         // red X
        textkb_close();
    }
}

void w_textkb(const char* title, const char* initial, int maxlen,
              w_textkb_cb_t on_ok) {
    textkb_close();
    w_modal_close();

    s_kb_cb = on_ok;

    s_kb_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_kb_bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_kb_bg, 0, 0);
    lv_obj_set_style_bg_color(s_kb_bg, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_kb_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb_bg, 0, 0);
    lv_obj_set_style_radius(s_kb_bg, 0, 0);
    lv_obj_set_style_pad_all(s_kb_bg, 0, 0);
    lv_obj_clear_flag(s_kb_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kb_bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* t = theme_label(s_kb_bg, title, FONT_MD, COL_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);

    s_kb_ta = lv_textarea_create(s_kb_bg);
    lv_obj_set_size(s_kb_ta, SCREEN_W - 80, 52);
    lv_obj_align(s_kb_ta, LV_ALIGN_TOP_MID, 0, 44);
    lv_textarea_set_one_line(s_kb_ta, true);
    lv_textarea_set_max_length(s_kb_ta, maxlen);
    lv_textarea_set_text(s_kb_ta, initial ? initial : "");
    lv_obj_set_style_bg_color(s_kb_ta, COL_CARD2, 0);
    lv_obj_set_style_border_color(s_kb_ta, COL_BLUE, 0);
    lv_obj_set_style_border_width(s_kb_ta, 2, 0);
    lv_obj_set_style_text_color(s_kb_ta, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_kb_ta, FONT_MD, 0);

    lv_obj_t* kb = lv_keyboard_create(s_kb_bg);
    lv_obj_set_size(kb, SCREEN_W, 300);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_kb_ta);
    lv_obj_add_event_cb(kb, textkb_event_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, textkb_event_cb, LV_EVENT_CANCEL, nullptr);
}

// ===========================================================================
//  SCROLLABLE PAGE
// ===========================================================================
lv_obj_t* w_page(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, SCREEN_W, SCREEN_H - CONTENT_Y);
    lv_obj_set_pos(p, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);

    // Vertical only: sideways drift on a settings list feels broken.
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    // A visible scrollbar, because on a touchscreen with no mouse wheel the
    // only cue that there is more content is the bar itself.
    lv_obj_set_style_bg_color(p, COL_CARD3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(p, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(p, 3, LV_PART_SCROLLBAR);
    return p;
}

void w_page_set_content_height(lv_obj_t* page, int h) {
    // An invisible spacer at the bottom is the simplest way to tell LVGL how
    // tall the content is when children are positioned absolutely.
    lv_obj_t* spacer = lv_obj_create(page);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(spacer, 0, h);
}
