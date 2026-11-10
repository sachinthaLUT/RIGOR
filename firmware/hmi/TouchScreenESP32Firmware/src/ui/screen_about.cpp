#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../link/link.h"
#include <stdio.h>

// ===========================================================================
//  ABOUT
//
//  Static information card. Nothing live, so there is no refresh hook -- the
//  link baud is read once at build time because it is a compile-time value.
//
//  Home lives in the TOP BAR here, exactly as on every other screen.
//
//  TWEAKING: the name, author, date and firmware version are defined once in
//  ui.h (APP_NAME, APP_AUTHOR, APP_DATE, HMI_FW_VERSION) and shared with the
//  splash, Settings and the Wi-Fi download page. Bump HMI_FW_VERSION when you
//  flash a new build, so this screen identifies what is actually running.
// ===========================================================================

lv_obj_t* screen_about_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "About", true);    // standard top-left Home pill

    // ---- Info card -------------------------------------------------------
    const int CW = 560;
    const int CH = 330;

    lv_obj_t* card = theme_card(scr);
    lv_obj_set_size(card, CW, CH);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 18);

    // Logo tile.
    lv_obj_t* sq = lv_obj_create(card);
    lv_obj_set_size(sq, 72, 72);
    lv_obj_set_style_bg_color(sq, COL_BLUE, 0);
    lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sq, 16, 0);
    lv_obj_set_style_border_width(sq, 0, 0);
    lv_obj_clear_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sq, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_center(theme_label(sq, LV_SYMBOL_DOWNLOAD, FONT_LG, COL_TEXT));

    lv_obj_align(theme_label(card, APP_NAME, FONT_XL, COL_TEXT),
                 LV_ALIGN_TOP_MID, 0, 88);

    lv_obj_align(theme_separator(card, 200), LV_ALIGN_TOP_MID, 0, 146);

    lv_obj_align(theme_label(card, APP_AUTHOR, FONT_MD, COL_MUTED),
                 LV_ALIGN_TOP_MID, 0, 166);
    lv_obj_align(theme_label(card, APP_DATE, FONT_MD, COL_MUTED),
                 LV_ALIGN_TOP_MID, 0, 198);

    // Panel firmware + link settings, useful when debugging in the lab.
    char b[64];
    snprintf(b, sizeof(b), "HMI firmware %s  -  link %lu baud",
             HMI_FW_VERSION, (unsigned long)link_baud());
    lv_obj_align(theme_label(card, b, FONT_XS, COL_FAINT),
                 LV_ALIGN_BOTTOM_MID, 0, 0);

    return scr;
}
