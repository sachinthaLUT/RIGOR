#include "ui.h"
#include "theme.h"

#include "img_rigor_logo.h"

// System version shown under the logo. (HMI firmware version is on About.)
#define SPLASH_VERSION  "V2.1"

static void splash_done(lv_timer_t* t) { lv_timer_del(t); ui_goto(SCREEN_HOME); }

// lv_anim exec callback for the progress bar.
static void bar_anim_cb(void* bar, int32_t v) {
    lv_bar_set_value((lv_obj_t*)bar, v, LV_ANIM_OFF);
}

lv_obj_t* screen_splash_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);

    // Logo bitmap (icon + RIGOR wordmark), see img_rigor_logo.cpp.
    lv_obj_t* logo = lv_img_create(scr);
    lv_img_set_src(logo, &img_rigor_logo);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -42);

    // Version under the wordmark.
    lv_obj_t* ver = theme_label(scr, SPLASH_VERSION, FONT_LG, COL_MUTED);
    lv_obj_set_style_text_letter_space(ver, 3, 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, 20);

    // Progress bar that fills over ~1.3 s.
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 320, 8);
    lv_obj_set_style_bg_color(bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 108);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, bar_anim_cb);
    lv_anim_set_time(&a, 1300);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);

    lv_obj_t* init = theme_label(scr, "Initialising system...", &lv_font_montserrat_14, COL_FAINT);
    lv_obj_align(init, LV_ALIGN_CENTER, 0, 138);

    lv_timer_t* t = lv_timer_create(splash_done, 1600, nullptr);
    lv_timer_set_repeat_count(t, 1);
    return scr;
}
