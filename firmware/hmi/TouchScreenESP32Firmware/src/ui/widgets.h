#pragma once
#include <lvgl.h>
#include "theme.h"

// ===========================================================================
//  WIDGETS  --  reusable building blocks shared by every screen.
//
//  RULE OF THUMB: if you find yourself copy-pasting more than ~5 lines of
//  LVGL styling between two screens, it belongs in here instead.
//
//  Everything here is plain LVGL v8. No SquareLine, no generated code.
// ===========================================================================

// ---------------------------------------------------------------------------
//  BUTTONS
// ---------------------------------------------------------------------------
enum WBtnStyle {
    WBTN_PRIMARY,    // solid blue   -- the main action on a screen
    WBTN_SECONDARY,  // dark grey    -- secondary / cancel
    WBTN_SUCCESS,    // solid green  -- start / execute / confirm
    WBTN_DANGER,     // red outline  -- stop / reset / destructive
    WBTN_GHOST       // outline only -- low emphasis
};

// Create a button with a centred label.
//   parent  : where to put it
//   txt     : label text (LV_SYMBOL_* prefixes are fine)
//   style   : see above
//   font    : nullptr -> FONT_MD
//   cb      : click handler (may be nullptr)
//   ud      : user data passed to the handler
lv_obj_t* w_button(lv_obj_t* parent, const char* txt, WBtnStyle style,
                   const lv_font_t* font, lv_event_cb_t cb, void* ud);

// Change the text of a button made by w_button().
void w_button_set_text(lv_obj_t* btn, const char* txt);

// Re-skin an existing button (e.g. START -> STOP).
void w_button_set_style(lv_obj_t* btn, WBtnStyle style);

// Grey a button out and stop it accepting touches (or re-enable it).
void w_button_enable(lv_obj_t* btn, bool enabled);

// ---------------------------------------------------------------------------
//  STEPPER   [ - ]  value  [ + ]
//  Returns the container; the value LABEL is returned via out_value_label so
//  you can update it. Width includes both buttons.
// ---------------------------------------------------------------------------
lv_obj_t* w_stepper(lv_obj_t* parent, int width, int height,
                    lv_event_cb_t on_minus, lv_event_cb_t on_plus,
                    void* ud, lv_obj_t** out_value_label);

// ---------------------------------------------------------------------------
//  BIG VALUE READOUT      1 8 . 7 8  kg
//                              12.3 N
//  Used by Static, Calibration and Diagnosis. Keep one struct per readout.
// ---------------------------------------------------------------------------
struct WReadout {
    lv_obj_t* cont  = nullptr;
    lv_obj_t* value = nullptr;   // the big number
    lv_obj_t* unit  = nullptr;   // small unit next to it
    lv_obj_t* sub   = nullptr;   // secondary line underneath
};

// Build a readout inside `parent`, sized `width` x `height`.
WReadout w_readout(lv_obj_t* parent, int width, int height,
                   const lv_font_t* value_font);

// Update it. `value_txt` is the big number, `unit_txt` the unit, `sub_txt`
// the smaller second line (pass nullptr to leave a field unchanged).
void w_readout_set(WReadout& r, const char* value_txt,
                   const char* unit_txt, const char* sub_txt);

// Recolour the unit + big number (e.g. grey out when the link is down).
void w_readout_set_color(WReadout& r, lv_color_t value_col, lv_color_t unit_col);

// ---------------------------------------------------------------------------
//  STATUS CHIP     (o) Stable
//  A small pill with a coloured dot and a label.
// ---------------------------------------------------------------------------
struct WChip {
    lv_obj_t* cont = nullptr;
    lv_obj_t* dot  = nullptr;
    lv_obj_t* lbl  = nullptr;
};

WChip w_chip(lv_obj_t* parent, const char* txt, lv_color_t col, int width);
void  w_chip_set(WChip& c, const char* txt, lv_color_t col);

// ---------------------------------------------------------------------------
//  KEY / VALUE ROW      Zero offset               0.03 kg
//  Left label muted, right value bold. Returns the VALUE label so you can
//  update it live. Rows are laid out top-down inside a card.
// ---------------------------------------------------------------------------
lv_obj_t* w_kv_row(lv_obj_t* card, const char* key, int y, int row_width);

// ---------------------------------------------------------------------------
//  SECTION CAPTION      LIVE FORCE
//  Small, muted, letter-spaced heading used inside cards.
// ---------------------------------------------------------------------------
lv_obj_t* w_caption(lv_obj_t* parent, const char* txt);

// ---------------------------------------------------------------------------
//  STAGE INDICATOR      (v)---( )---( )
//                      Moving Ramping Holding
//  Used on the Static screen to show where a run has got to.
// ---------------------------------------------------------------------------
#define W_STAGE_MAX 4

struct WStages {
    lv_obj_t* dot[W_STAGE_MAX]   = { nullptr };
    lv_obj_t* label[W_STAGE_MAX] = { nullptr };
    int       count              = 0;
};

// names[] must have `count` entries (<= W_STAGE_MAX).
WStages w_stages(lv_obj_t* parent, int width, const char* const* names, int count);

// active = index of the CURRENT stage. Everything before it shows as done,
// everything after as pending. Pass -1 for "nothing active yet".
void w_stages_set(WStages& s, int active);

// ---------------------------------------------------------------------------
//  MODAL DIALOG
//  Floats on lv_layer_top() so it survives screen changes and blocks touches
//  underneath. Only ONE modal exists at a time -- opening a new one replaces
//  the old. This is the single reusable path for every confirmation and
//  every error popup in the app.
// ---------------------------------------------------------------------------
typedef void (*w_modal_cb_t)(void);

// Show a dialog.
//   title      : short heading
//   msg        : body text (wraps automatically)
//   accent     : colour of the title + confirm button (COL_RED for faults)
//   ok_txt     : confirm button text; nullptr hides the confirm button
//   cancel_txt : cancel button text; nullptr hides it (dialog becomes
//                acknowledge-only)
//   on_ok      : called after the dialog closes, if confirm was pressed
void w_modal(const char* title, const char* msg, lv_color_t accent,
             const char* ok_txt, const char* cancel_txt, w_modal_cb_t on_ok);

// Close the current modal, if any. Safe to call when none is open.
void w_modal_close();

// Is a modal currently on screen? Used to avoid stacking fault popups.
bool w_modal_is_open();

// ---------------------------------------------------------------------------
//  NUMERIC KEYPAD  (modal)
//
//  A touch keypad for entering one number. Used by the Static screen's
//  "Custom" load and by the Dynamic step editor.
//
//  The SET button greys out whenever the entered value falls outside
//  [min_val, max_val], so an out-of-range figure can never be committed --
//  better than accepting it and letting the rig silently refuse.
//
//    title    : heading, e.g. "Custom Load"
//    unit     : shown next to the value, e.g. "kg"
//    initial  : value shown when it opens (<= 0 starts empty)
//    min_val  : lowest accepted value
//    max_val  : highest accepted value (e.g. RIG_MAX_KG)
//    decimals : 0 = integers only ('.' disabled), 1 = one decimal place
//    on_set   : called with the accepted value AFTER the keypad closes
// ---------------------------------------------------------------------------
typedef void (*w_keypad_cb_t)(float value);

void w_keypad(const char* title, const char* unit,
              float initial, float min_val, float max_val,
              int decimals, w_keypad_cb_t on_set);

// ---------------------------------------------------------------------------
//  TEXT KEYBOARD  (modal)
//
//  On-screen QWERTY for entering a short string -- used for the dynamic test
//  name, which becomes the CSV filename.
//
//  Characters FAT cannot store are simply absent from the keyboard rather
//  than rejected after typing, so an invalid name is not reachable.
// ---------------------------------------------------------------------------
typedef void (*w_textkb_cb_t)(const char* text);

void w_textkb(const char* title, const char* initial, int maxlen,
              w_textkb_cb_t on_ok);

// ---------------------------------------------------------------------------
//  SCROLLABLE PAGE
//
//  A full-width container below the top bar that scrolls vertically when its
//  contents overflow. Used by Settings, which now has more rows than fit.
//
//  Add children with normal lv_obj_align(..., LV_ALIGN_TOP_LEFT, x, y) and set
//  the total content height with w_page_set_content_height() so LVGL knows how
//  far it can scroll.
// ---------------------------------------------------------------------------
lv_obj_t* w_page(lv_obj_t* parent);
void      w_page_set_content_height(lv_obj_t* page, int h);

// ---------------------------------------------------------------------------
//  TOAST
//  Transient, non-blocking message that fades away by itself. Use for
//  "Command sent", "Retract complete" -- anything that does NOT need a
//  decision from the operator.
// ---------------------------------------------------------------------------
void w_toast(const char* txt, lv_color_t accent, uint32_t ms);
