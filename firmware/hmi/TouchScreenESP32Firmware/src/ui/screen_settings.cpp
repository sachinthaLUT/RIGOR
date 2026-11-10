#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../app/datalog.h"
#include "../link/link.h"
#include <stdio.h>

// ===========================================================================
//  SETTINGS
//
//  NOW SCROLLS. There are more rows than fit in 480 px, and squeezing them
//  would push each row below a comfortable touch height. w_page() gives a
//  vertical scroll container with a visible bar -- on a touchscreen with no
//  mouse wheel, that bar is the only cue that more content exists.
//
//  WHAT IS DELIBERATELY NOT HERE
//  "Force ramp rate" and "Max force limit" live in the RIG firmware
//  (PWM_SLEW_PER_TICK, MAX_SETPOINT_KG) with no runtime setter, so editing
//  them here would change nothing at all. Max setpoint is shown read-only.
//  The driver temperature and current limits are the same: compiled into
//  the rig, not editable here.
//
//  Screen brightness was removed (10/2026): the panel runs at 100 %.
//
//  BUZZER is Off / Low / High. The rig owns the buzzer, so the level is sent
//  with 'V' and re-sent by link.cpp on every reconnect. A short test beep is
//  played whenever the level is changed, so the operator hears the result.
// ===========================================================================

static const int ROW_H = 64;

static lv_obj_t* s_seg_kg     = nullptr;
static lv_obj_t* s_seg_n      = nullptr;
static lv_obj_t* s_seg_buzz[3] = { nullptr };
static lv_obj_t* s_sw_confirm = nullptr;
static lv_obj_t* s_sw_retract = nullptr;
static lv_obj_t* s_sw_estop   = nullptr;
static lv_obj_t* s_sd_lbl     = nullptr;

// ---------------------------------------------------------------------------
//  VIEW SYNC
// ---------------------------------------------------------------------------
static void sync_units() {
    const bool kg = (g_settings.units == Units::Kilogram);
    lv_obj_set_style_bg_color(s_seg_kg, kg ? COL_BLUE  : COL_CARD2, 0);
    lv_obj_set_style_bg_color(s_seg_n,  kg ? COL_CARD2 : COL_BLUE,  0);
}
static void sync_buzz() {
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_bg_color(s_seg_buzz[i],
            (i == g_settings.buzzer_level) ? COL_BLUE : COL_CARD2, 0);
}
static void sync_switch(lv_obj_t* sw, bool on) {
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else    lv_obj_clear_state(sw, LV_STATE_CHECKED);
}
static void sync_sd() {
    if (!s_sd_lbl) return;
    char b[64];
    if (datalog_has_card())
        snprintf(b, sizeof(b), "%d file(s), %lu of %lu MB free",
                 datalog_file_count(),
                 (unsigned long)datalog_free_mb(), (unsigned long)datalog_total_mb());
    else
        snprintf(b, sizeof(b), "No card  -  32 GB+ must be FAT32");
    lv_label_set_text(s_sd_lbl, b);
}

// ---------------------------------------------------------------------------
//  CALLBACKS
// ---------------------------------------------------------------------------
static void units_kg_cb(lv_event_t*) { g_settings.units = Units::Kilogram; sync_units(); settings_save(); }
static void units_n_cb (lv_event_t*) { g_settings.units = Units::Newton;   sync_units(); settings_save(); }

static void buzz_cb(lv_event_t* e) {
    const uint8_t lv = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (lv == g_settings.buzzer_level) return;
    g_settings.buzzer_level = lv;
    sync_buzz();
    settings_save();
    link_cmd_buzzer_level(lv);
    link_cmd_beep();                 // test beep at the new level (none if Off)
}

static void confirm_cb(lv_event_t* e) {
    g_settings.confirm_high = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    settings_save();
}

static void retract_cb(lv_event_t* e) {
    g_settings.auto_retract_on_loss =
        lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    settings_save();
    link_cmd_loss_action(g_settings.auto_retract_on_loss);
}

// Retract after an e-stop reset. Like the link-loss setting, the action runs
// on the rig (it does the retract when it receives 'Z'), so the preference is
// sent now and re-sent by link.cpp on every reconnect.
static void estop_cb(lv_event_t* e) {
    g_settings.retract_after_estop =
        lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    settings_save();
    link_cmd_estop_action(g_settings.retract_after_estop);
}

static void erase_confirmed() {
    const int n = datalog_erase_all();
    sync_sd();
    char b[40]; snprintf(b, sizeof(b), "%d file(s) erased", n);
    w_toast(b, COL_GREEN, 2500);
}
static void erase_cb(lv_event_t*) {
    if (!datalog_has_card()) { w_toast("No SD card", COL_ORANGE, 2000); return; }
    w_modal("Erase SD card?",
            "Every recorded test file will be deleted.\n\nThis cannot be undone.",
            COL_RED, "Erase all", "Cancel", erase_confirmed);
}

static void restore_confirmed() {
    settings_defaults();
    sync_units();
    sync_buzz();
    sync_switch(s_sw_confirm, g_settings.confirm_high);
    sync_switch(s_sw_retract, g_settings.auto_retract_on_loss);
    sync_switch(s_sw_estop,   g_settings.retract_after_estop);
    settings_save();
    link_cmd_buzzer_level(g_settings.buzzer_level);
    link_cmd_loss_action(g_settings.auto_retract_on_loss);
    link_cmd_estop_action(g_settings.retract_after_estop);
    w_toast("Settings restored", COL_GREEN, 2000);
}
static void restore_cb(lv_event_t*) {
    w_modal("Restore defaults?",
            "Settings return to factory values.\n\n"
            "Saved profiles and SD files are not affected.",
            COL_BLUE, "Restore", "Cancel", restore_confirmed);
}

// ---------------------------------------------------------------------------
//  ROW HELPERS  (all positioned inside the scrolling page)
// ---------------------------------------------------------------------------
static lv_obj_t* row_card(lv_obj_t* page, int y, int h) {
    lv_obj_t* c = theme_card(page);
    lv_obj_set_size(c, CONTENT_W, h);
    lv_obj_set_pos(c, PAD_EDGE, y);
    lv_obj_set_style_pad_all(c, 16, 0);
    return c;
}
static void row_title(lv_obj_t* card, const char* txt, const char* sub) {
    lv_obj_align(theme_label(card, txt, FONT_MD, COL_TEXT),
                 LV_ALIGN_LEFT_MID, 0, sub ? -10 : 0);
    if (sub) lv_obj_align(theme_label(card, sub, FONT_XS, COL_FAINT),
                          LV_ALIGN_LEFT_MID, 0, 12);
}
static lv_obj_t* make_switch(lv_obj_t* card, bool on, lv_event_cb_t cb) {
    lv_obj_t* sw = lv_switch_create(card);
    lv_obj_set_size(sw, 60, 32);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    sync_switch(sw, on);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

// ---------------------------------------------------------------------------
//  BUILD
// ---------------------------------------------------------------------------
lv_obj_t* screen_settings_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Settings", true);

    lv_obj_t* page = w_page(scr);
    int y = 0;

    // ---- Units ------------------------------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H);
        row_title(c, "Force units", nullptr);
        lv_obj_t* seg = lv_obj_create(c);
        lv_obj_set_size(seg, 140, 40);
        lv_obj_set_style_bg_color(seg, COL_CARD2, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seg, RADIUS_CTRL, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 3, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(seg, LV_ALIGN_RIGHT_MID, 0, 0);

        s_seg_kg = w_button(seg, "kg", WBTN_SECONDARY, FONT_MD, units_kg_cb, nullptr);
        lv_obj_set_size(s_seg_kg, 65, 34);
        lv_obj_align(s_seg_kg, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(s_seg_kg, 8, 0);

        s_seg_n = w_button(seg, "N", WBTN_SECONDARY, FONT_MD, units_n_cb, nullptr);
        lv_obj_set_size(s_seg_n, 65, 34);
        lv_obj_align(s_seg_n, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(s_seg_n, 8, 0);
        sync_units();
        y += ROW_H + 8;
    }

    // ---- Buzzer volume ----------------------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H);
        row_title(c, "Buzzer volume", "Alerts and alarms on the rig");
        lv_obj_t* seg = lv_obj_create(c);
        lv_obj_set_size(seg, 246, 40);
        lv_obj_set_style_bg_color(seg, COL_CARD2, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seg, RADIUS_CTRL, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 3, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(seg, LV_ALIGN_RIGHT_MID, 0, 0);

        static const char* const NAMES[3] = { "Off", "Low", "High" };
        for (int i = 0; i < 3; i++) {
            s_seg_buzz[i] = w_button(seg, NAMES[i], WBTN_SECONDARY, FONT_MD,
                                     buzz_cb, (void*)(intptr_t)i);
            lv_obj_set_size(s_seg_buzz[i], 78, 34);
            lv_obj_align(s_seg_buzz[i], LV_ALIGN_LEFT_MID, i * 80, 0);
            lv_obj_set_style_radius(s_seg_buzz[i], 8, 0);
        }
        sync_buzz();
        y += ROW_H + 8;
    }

    // ---- Confirm high loads -----------------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H);
        row_title(c, "Confirm loads over 50 kg", "Extra tap before a heavy run");
        s_sw_confirm = make_switch(c, g_settings.confirm_high, confirm_cb);
        y += ROW_H + 8;
    }

    // ---- Auto-retract on link loss ----------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H + 6);
        row_title(c, "Retract if link is lost",
                  "Off = hold position.  On = retract 5 mm to unload.");
        s_sw_retract = make_switch(c, g_settings.auto_retract_on_loss, retract_cb);
        y += ROW_H + 14;
    }

    // ---- Retract after e-stop reset ---------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H + 6);
        row_title(c, "Retract after E-stop reset",
                  "On = retract 5 mm when you reset.  Off = clear the fault only.");
        s_sw_estop = make_switch(c, g_settings.retract_after_estop, estop_cb);
        y += ROW_H + 14;
    }

    // ---- SD card ----------------------------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H + 6);
        lv_obj_align(theme_label(c, "Test data (SD card)", FONT_MD, COL_TEXT),
                     LV_ALIGN_LEFT_MID, 0, -10);
        s_sd_lbl = theme_label(c, "--", FONT_XS, COL_FAINT);
        lv_obj_align(s_sd_lbl, LV_ALIGN_LEFT_MID, 0, 12);

        lv_obj_t* e = w_button(c, "Erase SD", WBTN_DANGER, FONT_SM, erase_cb, nullptr);
        lv_obj_set_size(e, 130, 44);
        lv_obj_align(e, LV_ALIGN_RIGHT_MID, 0, 0);
        sync_sd();
        y += ROW_H + 14;
    }

    // ---- Rig limit (read-only fact) ---------------------------------------
    {
        lv_obj_t* c = row_card(page, y, ROW_H);
        row_title(c, "Rig max setpoint", "Compiled into the rig firmware");
        char b[24]; snprintf(b, sizeof(b), "%.0f kg", RIG_MAX_KG);
        lv_obj_align(theme_label(c, b, FONT_MD, COL_MUTED), LV_ALIGN_RIGHT_MID, 0, 0);
        y += ROW_H + 8;
    }

    // ---- Restore defaults -------------------------------------------------
    {
        lv_obj_t* rb = w_button(page, LV_SYMBOL_REFRESH "  Restore Defaults",
                                WBTN_GHOST, FONT_SM, restore_cb, nullptr);
        lv_obj_set_size(rb, 240, 50);
        lv_obj_set_pos(rb, SCREEN_W - PAD_EDGE - 240, y);
        y += 50 + 16;
    }

    // ---- ABOUT ------------------------------------------------------------
    {
        lv_obj_t* c = row_card(page, y, 150);
        lv_obj_align(w_caption(c, "ABOUT"), LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_align(theme_label(c, APP_NAME, FONT_LG, COL_TEXT),
                     LV_ALIGN_TOP_LEFT, 0, 22);
        lv_obj_align(theme_label(c, APP_AUTHOR, FONT_SM, COL_MUTED),
                     LV_ALIGN_TOP_LEFT, 0, 60);
        lv_obj_align(theme_label(c, APP_DATE, FONT_SM, COL_MUTED),
                     LV_ALIGN_TOP_LEFT, 0, 84);
        char b[72];
        snprintf(b, sizeof(b), "HMI firmware %s  -  link %lu baud",
                 HMI_FW_VERSION, (unsigned long)link_baud());
        lv_obj_align(theme_label(c, b, FONT_XS, COL_FAINT), LV_ALIGN_TOP_LEFT, 0, 110);
        y += 150 + 16;
    }

    w_page_set_content_height(page, y);
    return scr;
}

void screen_settings_refresh() { sync_sd(); }
