#pragma once
#include <lvgl.h>

// ===========================================================================
//  THEME  --  single source of truth for the look of the whole UI.
//  Change a value here and every screen follows. Nothing else should
//  hard-code a colour or a font.
// ===========================================================================

// ---------------------------------------------------------------------------
//  COLOUR TOKENS
//  Named by ROLE, not by hue, so you can re-skin without renaming anything.
// ---------------------------------------------------------------------------
#define COL_BG       lv_color_hex(0x0E1217)   // app background (darkest)
#define COL_CARD     lv_color_hex(0x171D29)   // panel / card surface
#define COL_CARD2    lv_color_hex(0x1F2736)   // inset control background
#define COL_CARD3    lv_color_hex(0x2A3242)   // pressed / raised control

#define COL_BLUE     lv_color_hex(0x2E8FD6)   // primary action
#define COL_BLUE_DK  lv_color_hex(0x2575B0)   // primary pressed
#define COL_BLUE_BG  lv_color_hex(0x16283A)   // tinted background (selected row)

#define COL_GREEN    lv_color_hex(0x2EB36B)   // OK / stable / go
#define COL_GREEN_DK lv_color_hex(0x248A53)
#define COL_GREEN_BG lv_color_hex(0x13251C)

#define COL_ORANGE   lv_color_hex(0xE89A12)   // ramping / warning
#define COL_ORANGE_BG lv_color_hex(0x2A2113)

#define COL_RED      lv_color_hex(0xE05A4E)   // stop / fault / danger
#define COL_RED_BG   lv_color_hex(0x241314)
#define COL_RED_BD   lv_color_hex(0x6E2A24)

#define COL_TEXT     lv_color_hex(0xFFFFFF)   // primary text
#define COL_MUTED    lv_color_hex(0x8A93A3)   // secondary text
#define COL_FAINT    lv_color_hex(0x5A6273)   // tertiary / disabled text
#define COL_TRACK    lv_color_hex(0x2A3240)   // empty bar / slider track
#define COL_LINE     lv_color_hex(0x2A3240)   // hairline separators

// ---------------------------------------------------------------------------
//  FONT TOKENS  (with automatic fallback)
//
//  LVGL only compiles in the Montserrat sizes you enable in lv_conf.h. The
//  #if chains below pick the closest ENABLED size, so this project builds even
//  if a size is missing -- it just looks smaller than intended.
//
//  RECOMMENDED lv_conf.h settings for this UI:
//      #define LV_FONT_MONTSERRAT_14  1
//      #define LV_FONT_MONTSERRAT_16  1
//      #define LV_FONT_MONTSERRAT_20  1
//      #define LV_FONT_MONTSERRAT_28  1
//      #define LV_FONT_MONTSERRAT_40  1
//      #define LV_FONT_MONTSERRAT_48  1   <-- needed for the big force readout
//
//  If FONT_HUGE silently falls back to 40 the layout still works; the number
//  is just less dominant.
// ---------------------------------------------------------------------------
#if LV_FONT_MONTSERRAT_14
  #define FONT_XS   &lv_font_montserrat_14
#else
  #define FONT_XS   LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_16
  #define FONT_SM   &lv_font_montserrat_16
#else
  #define FONT_SM   FONT_XS
#endif

#if LV_FONT_MONTSERRAT_20
  #define FONT_MD   &lv_font_montserrat_20
#else
  #define FONT_MD   FONT_SM
#endif

#if LV_FONT_MONTSERRAT_28
  #define FONT_LG   &lv_font_montserrat_28
#else
  #define FONT_LG   FONT_MD
#endif

#if LV_FONT_MONTSERRAT_40
  #define FONT_XL   &lv_font_montserrat_40
#else
  #define FONT_XL   FONT_LG
#endif

#if LV_FONT_MONTSERRAT_48
  #define FONT_HUGE &lv_font_montserrat_48
#elif LV_FONT_MONTSERRAT_44
  #define FONT_HUGE &lv_font_montserrat_44
#else
  #define FONT_HUGE FONT_XL
#endif

// ---------------------------------------------------------------------------
//  GEOMETRY  (panel is 800 x 480)
//  Every screen lays out below TOPBAR_H, inside PAD_EDGE margins.
// ---------------------------------------------------------------------------
#define SCREEN_W    800
#define SCREEN_H    480
#define TOPBAR_H    56    // height reserved for the title bar
#define PAD_EDGE    14    // outer margin on all screens
#define PAD_GAP     12    // standard gap between sibling elements
#define RADIUS_CARD 14    // card corner radius
#define RADIUS_CTRL 10    // button / control corner radius

// Content area below the top bar (use these instead of magic numbers).
#define CONTENT_Y   (TOPBAR_H + 6)
#define CONTENT_H   (SCREEN_H - CONTENT_Y - PAD_EDGE)
#define CONTENT_W   (SCREEN_W - 2 * PAD_EDGE)

// Minimum comfortable touch target. Do not make tappable things smaller.
#define TOUCH_MIN   44

// ---------------------------------------------------------------------------
//  BASIC BUILDERS
// ---------------------------------------------------------------------------

// Paint a screen object with the app background and disable scrolling.
void theme_screen(lv_obj_t* scr);

// A rounded dark card. Size/position it yourself after creation.
lv_obj_t* theme_card(lv_obj_t* parent);

// A label with font + colour applied in one call.
lv_obj_t* theme_label(lv_obj_t* parent, const char* txt,
                      const lv_font_t* font, lv_color_t color);

// A 1px horizontal separator line inside a card.
lv_obj_t* theme_separator(lv_obj_t* parent, int width);

// ---------------------------------------------------------------------------
//  TOP BAR
//  Centred title, optional Home button (top-left), and a live link indicator
//  (top-right) that turns amber when the rig link drops.
//
//  The link dot is refreshed automatically by the global UI tick -- you do not
//  need to touch it from your screen code.
// ---------------------------------------------------------------------------
lv_obj_t* ui_topbar(lv_obj_t* scr, const char* title, bool show_home);

// Called by the global tick to update the link dot of the ACTIVE screen.
void ui_topbar_refresh(lv_obj_t* scr);
