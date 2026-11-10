#include "theme.h"
#include "ui.h"
#include "../app/state.h"
#include <Arduino.h>

// ===========================================================================
//  BASIC BUILDERS
// ===========================================================================

void theme_screen(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* theme_card(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(c, PAD_EDGE, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t* theme_label(lv_obj_t* parent, const char* txt,
                      const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t* theme_separator(lv_obj_t* parent, int width) {
    lv_obj_t* s = lv_obj_create(parent);
    lv_obj_set_size(s, width, 1);
    lv_obj_set_style_bg_color(s, COL_LINE, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_radius(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

// ===========================================================================
//  TOP BAR
//
//  Each screen gets its own top bar, so the link indicator is a DIFFERENT
//  widget on every screen. A tiny registry maps screen -> link label, and the
//  global UI tick calls ui_topbar_refresh(lv_scr_act()).
// ===========================================================================
struct TopbarEntry {
    lv_obj_t* scr  = nullptr;
    lv_obj_t* link = nullptr;
};
static TopbarEntry s_bars[SCREEN_COUNT];
static int         s_bar_count = 0;

static void topbar_home_cb(lv_event_t* /*e*/) {
    ui_goto(SCREEN_HOME);
}

lv_obj_t* ui_topbar(lv_obj_t* scr, const char* title, bool show_home) {
    lv_obj_t* t = theme_label(scr, title, FONT_LG, COL_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t* rule = theme_separator(scr, SCREEN_W);
    lv_obj_align(rule, LV_ALIGN_TOP_LEFT, 0, TOPBAR_H);

    if (show_home) {
        lv_obj_t* b = lv_btn_create(scr);
        lv_obj_set_size(b, LV_SIZE_CONTENT, TOUCH_MIN);
        lv_obj_set_style_bg_color(b, COL_CARD2, 0);
        lv_obj_set_style_bg_color(b, COL_CARD3, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, TOUCH_MIN / 2, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, PAD_EDGE, 6);
        lv_obj_add_event_cb(b, topbar_home_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_center(theme_label(b, LV_SYMBOL_HOME "  Home", FONT_MD, COL_TEXT));
    }

    // --- Link indicator (top-right). Also becomes the E-STOP badge. -------
    lv_obj_t* link = theme_label(scr, LV_SYMBOL_WIFI "  Rig", FONT_XS, COL_FAINT);
    lv_obj_set_style_radius(link, 12, 0);
    lv_obj_align(link, LV_ALIGN_TOP_RIGHT, -PAD_EDGE, 20);

    if (s_bar_count < SCREEN_COUNT) {
        s_bars[s_bar_count].scr  = scr;
        s_bars[s_bar_count].link = link;
        s_bar_count++;
    }
    return t;
}

// Badge styling for the indicator: a filled red pill for the e-stop, plain
// coloured text otherwise.
static void indicator_badge(lv_obj_t* l, bool on) {
    if (on) {
        lv_obj_set_style_bg_color(l, COL_RED, 0);
        lv_obj_set_style_bg_opa(l, ((millis() / 500) % 2) ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_set_style_pad_hor(l, 10, 0);
        lv_obj_set_style_pad_ver(l, 4, 0);
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -PAD_EDGE, 15);
    } else {
        lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_hor(l, 0, 0);
        lv_obj_set_style_pad_ver(l, 0, 0);
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -PAD_EDGE, 20);
    }
}

void ui_topbar_refresh(lv_obj_t* scr) {
    for (int i = 0; i < s_bar_count; i++) {
        if (s_bars[i].scr != scr) continue;
        lv_obj_t* l = s_bars[i].link;
        if (!l) return;
        if (g_app.link_ok && g_app.estop) {
            lv_label_set_text(l, LV_SYMBOL_WARNING "  E-STOP");
            lv_obj_set_style_text_color(l, COL_TEXT, 0);
            indicator_badge(l, true);
        } else if (g_app.link_ok) {
            lv_label_set_text(l, LV_SYMBOL_WIFI "  Rig");
            lv_obj_set_style_text_color(l, COL_GREEN, 0);
            indicator_badge(l, false);
        } else {
            lv_label_set_text(l, LV_SYMBOL_WARNING "  No link");
            lv_obj_set_style_text_color(l, COL_ORANGE, 0);
            indicator_badge(l, false);
        }
        return;
    }
}
