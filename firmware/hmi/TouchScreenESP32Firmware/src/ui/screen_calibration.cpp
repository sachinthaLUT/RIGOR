#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  CALIBRATION  (rebuilt 10/2026 to match LoadCellCalibrationFirmware.ino)
//
//  Left  : LIVE raw counts, the kg they convert to, and a 5 s noise window
//          (min / max / peak-to-peak / standard deviation), exactly like the
//          PC utility's TEST mode.
//  Right : CAPTURE. Each button averages the counts for 5 s, like the PC
//          utility's M / R keys, and reports n, p2p and SD so a noisy capture
//          is obvious. Applied - Removed gives the net counts and net kg.
//  Bottom: [Capture APPLIED]  [Capture REMOVED]
//
//  WHERE THE NUMBERS COME FROM
//  The rig sends N=<counts> in every telemetry frame (rig firmware rev 6.1):
//  the mean of the ADS1220 conversions in one 20 ms control tick. While this
//  screen is open the telemetry rate is raised to CAL_TELEM_HZ, so a 5 s
//  capture averages ~100 frames. It drops back when you leave.
//
//  WHAT THIS SCREEN CANNOT DO
//  The calibration constants k and c are compile-time values in the rig
//  firmware (CAL_SLOPE_KG_PER_COUNT, CAL_INTERCEPT_KG). This screen shows the
//  counts and captures to fit them, but cannot write them; the values shown
//  are mirrored in state.h (RIG_CAL_*). Keep the two in sync.
//
//  TARE (rig rev 6.2)
//  Zero / Clear in the left card send O1 / O0. The rig averages 0.5 s and
//  stores a zero offset in flash, subtracted ON TOP of k and c -- so the
//  published calibration never changes, only where zero sits in this fixture.
//  The kg shown here use the same tare as the rig, so they agree with F.
// ===========================================================================

// ---- Tuning ----------------------------------------------------------------
static const uint32_t CAPTURE_MS     = 5000;   // = AVG_WINDOW_MS in the PC tool
static const uint32_t WINDOW_MS      = 5000;   // = PP_WINDOW_MS in the PC tool
static const int      CAL_TELEM_HZ   = 20;     // while this screen is open
static const int      IDLE_TELEM_HZ  = 4;      // the rig's normal idle rate
static const float    NOISY_P2P_KG   = 0.05f;  // capture flagged above this
static const uint32_t MIN_SAMPLES    = 10;     // fewer = capture refused

// ---- Layout ----------------------------------------------------------------
static const int LEFT_W  = (CONTENT_W - PAD_GAP) / 2;      // 380
static const int RIGHT_X = PAD_EDGE + LEFT_W + PAD_GAP;
static const int RIGHT_W = SCREEN_W - RIGHT_X - PAD_EDGE;
static const int BTN_H   = 64;
static const int TOP_H   = CONTENT_H - BTN_H - PAD_GAP;
static const int IW      = LEFT_W - 2 * PAD_EDGE;          // card inner width
static const int COL_CNT = 196;   // right edge of the "counts" column (capture)

// ---- Widgets ---------------------------------------------------------------
static lv_obj_t* s_live_counts = nullptr;
static lv_obj_t* s_live_dot    = nullptr;
static lv_obj_t* s_live_kg     = nullptr;
static lv_obj_t* s_live_rig    = nullptr;
static lv_obj_t* s_win_cap     = nullptr;
static lv_obj_t* s_win_minmax  = nullptr;
static lv_obj_t* s_win_p2p     = nullptr;
static lv_obj_t* s_win_sd      = nullptr;
static lv_obj_t* s_win_n       = nullptr;

static lv_obj_t* s_cap_bar     = nullptr;
static lv_obj_t* s_cap_status  = nullptr;
static lv_obj_t* s_row_cnt[3]  = { nullptr };   // applied, removed, net
static lv_obj_t* s_row_kg[3]   = { nullptr };
static lv_obj_t* s_btn_applied = nullptr;
static lv_obj_t* s_btn_removed = nullptr;
static lv_obj_t* s_tare_lbl    = nullptr;

// ---- State -----------------------------------------------------------------
enum CapKind { CAP_NONE = 0, CAP_APPLIED = 1, CAP_REMOVED = 2 };
static CapKind  s_capturing   = CAP_NONE;
static uint32_t s_cap_start   = 0;
static uint32_t s_win_start   = 0;
static bool     s_raised_rate = false;

static bool   s_have[3]   = { false, false, false };     // applied, removed
static double s_mean[3]   = { 0, 0, 0 };

static inline double counts_to_kg(double c) {
    return RIG_CAL_SLOPE_KG_PER_COUNT * c + RIG_CAL_INTERCEPT_KG - g_app.tare_kg;
}

// ---------------------------------------------------------------------------
//  ENTER / LEAVE  (called by ui_goto)
// ---------------------------------------------------------------------------
void screen_calibration_enter() {
    g_app.cnt_window.reset();
    s_win_start = millis();
    // Leave the rate alone during a run: a dynamic test owns it then.
    if (g_app.link_ok && g_app.rig != RigState::Running) {
        link_cmd_telemetry_hz(CAL_TELEM_HZ);
        s_raised_rate = true;
    }
}

void screen_calibration_leave() {
    s_capturing = CAP_NONE;
    if (s_raised_rate && g_app.rig != RigState::Running)
        link_cmd_telemetry_hz(IDLE_TELEM_HZ);
    s_raised_rate = false;
}

// ---------------------------------------------------------------------------
//  CAPTURE TABLE
// ---------------------------------------------------------------------------
static void set_cell(lv_obj_t* l, const char* txt, lv_color_t col) {
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
}

static void paint_captures() {
    char b[24];
    for (int i = 0; i < 2; i++) {
        if (s_have[i]) {
            snprintf(b, sizeof(b), "%ld", (long)lround(s_mean[i]));
            set_cell(s_row_cnt[i], b, COL_TEXT);
            snprintf(b, sizeof(b), "%.3f", counts_to_kg(s_mean[i]));
            set_cell(s_row_kg[i], b, COL_TEXT);
        } else {
            set_cell(s_row_cnt[i], "--", COL_FAINT);
            set_cell(s_row_kg[i],  "--", COL_FAINT);
        }
    }
    // Net = applied - removed: the intercept and any standing offset cancel,
    // so this is the force the known mass actually produced.
    if (s_have[0] && s_have[1]) {
        const double dn = s_mean[0] - s_mean[1];
        snprintf(b, sizeof(b), "%ld", (long)lround(dn));
        set_cell(s_row_cnt[2], b, COL_GREEN);
        snprintf(b, sizeof(b), "%.3f", RIG_CAL_SLOPE_KG_PER_COUNT * dn);
        set_cell(s_row_kg[2], b, COL_GREEN);
    } else {
        set_cell(s_row_cnt[2], "--", COL_FAINT);
        set_cell(s_row_kg[2],  "--", COL_FAINT);
    }
}

static void start_capture(CapKind k) {
    if (!g_app.link_ok) { w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000); return; }
    if (!g_app.has_counts) { w_toast("Rig firmware rev 6.1 needed (N= counts)", COL_ORANGE, 3000); return; }
    if (s_capturing != CAP_NONE) { w_toast("A capture is already running", COL_ORANGE, 1800); return; }

    g_app.cnt_capture.reset();
    s_capturing = k;
    s_cap_start = millis();
    lv_obj_clear_flag(s_cap_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_cap_bar, 0, LV_ANIM_OFF);
}

static void applied_cb(lv_event_t*) { start_capture(CAP_APPLIED); }
static void removed_cb(lv_event_t*) { start_capture(CAP_REMOVED); }

static void finish_capture() {
    const CountStats st = g_app.cnt_capture;
    const int idx = (s_capturing == CAP_APPLIED) ? 0 : 1;
    const char* name = (idx == 0) ? "APPLIED" : "REMOVED";
    s_capturing = CAP_NONE;
    lv_obj_add_flag(s_cap_bar, LV_OBJ_FLAG_HIDDEN);

    char b[96];
    if (st.n < MIN_SAMPLES) {
        snprintf(b, sizeof(b), "%s failed: only %lu samples", name, (unsigned long)st.n);
        set_cell(s_cap_status, b, COL_RED);
        w_toast("Capture failed - telemetry too slow", COL_RED, 2500);
        return;
    }

    s_mean[idx] = st.mean();
    s_have[idx] = true;
    paint_captures();

    const float p2p_kg = (float)(st.p2p() * RIG_CAL_SLOPE_KG_PER_COUNT);
    snprintf(b, sizeof(b), "%s  n=%lu  p2p=%ld  sd=%.1f%s",
             name, (unsigned long)st.n, (long)st.p2p(), st.sd(),
             st.saturated ? "  SATURATED" : (p2p_kg > NOISY_P2P_KG ? "  noisy" : ""));
    set_cell(s_cap_status, b,
             st.saturated ? COL_RED : (p2p_kg > NOISY_P2P_KG ? COL_ORANGE : COL_GREEN));

    if (st.saturated)
        w_modal("ADC saturated",
                "The ADS1220 hit full scale during the capture, so the value is "
                "invalid.\n\nCheck the load cell wiring and the reference.",
                COL_RED, "OK", nullptr, nullptr);
    else
        w_toast(idx == 0 ? "Applied captured" : "Removed captured", COL_BLUE, 1500);
}

static void clear_confirmed() {
    s_have[0] = s_have[1] = false;
    paint_captures();
    set_cell(s_cap_status, "Ready", COL_MUTED);
}

static void clear_cb(lv_event_t*) {
    w_modal("Clear captures?", "Both captured averages will be discarded.",
            COL_ORANGE, "Clear", "Cancel", clear_confirmed);
}

// ---------------------------------------------------------------------------
//  TARE
// ---------------------------------------------------------------------------
static bool tare_guard() {
    if (!g_app.link_ok)  { w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000); return false; }
    if (!g_app.has_tare) { w_toast("Tare needs rig firmware rev 6.2", COL_ORANGE, 2500); return false; }
    if (g_app.rig == RigState::Running) { w_toast("Stop the run first", COL_ORANGE, 2000); return false; }
    if (g_app.rig == RigState::Fault)   { w_toast("Clear the fault first", COL_ORANGE, 2000); return false; }
    return true;
}

static void tare_confirmed() {
    link_cmd_tare(true);
    w_toast("Zeroing - keep the cell unloaded", COL_BLUE, 2000);
}
static void tare_cb(lv_event_t*) {
    if (!tare_guard()) return;
    w_modal("Zero the reading?",
            "Remove everything from the load cell first.\n\n"
            "The calibration (k, c) is not changed.",
            COL_BLUE, "Zero", "Cancel", tare_confirmed);
}

static void tare_clear_confirmed() {
    link_cmd_tare(false);
    w_toast("Tare cleared", COL_BLUE, 1800);
}
static void tare_clear_cb(lv_event_t*) {
    if (!g_app.link_ok)  { w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000); return; }
    if (!g_app.has_tare) { w_toast("Tare needs rig firmware rev 6.2", COL_ORANGE, 2500); return; }
    w_modal("Clear the tare?", "The reading goes back to the plain calibration (k, c).",
            COL_ORANGE, "Clear", "Cancel", tare_clear_confirmed);
}

// ---------------------------------------------------------------------------
//  BUILD
// ---------------------------------------------------------------------------
// A capture-table row: name on the left, counts and kg right-aligned columns.
static void cap_row(lv_obj_t* card, int y, const char* name, lv_color_t name_col,
                    lv_obj_t** cnt, lv_obj_t** kg) {
    lv_obj_align(theme_label(card, name, FONT_SM, name_col), LV_ALIGN_TOP_LEFT, 0, y);
    *cnt = theme_label(card, "--", FONT_MD, COL_FAINT);
    lv_obj_align(*cnt, LV_ALIGN_TOP_RIGHT, -(IW - COL_CNT), y - 2);
    *kg = theme_label(card, "--", FONT_MD, COL_FAINT);
    lv_obj_align(*kg, LV_ALIGN_TOP_RIGHT, 0, y - 2);
}

lv_obj_t* screen_calibration_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Calibration", true);

    // ================= LEFT: live counts + noise window ==================
    lv_obj_t* live = theme_card(scr);
    lv_obj_set_size(live, LEFT_W, TOP_H);
    lv_obj_set_pos(live, PAD_EDGE, CONTENT_Y);

    lv_obj_align(w_caption(live, "LIVE COUNTS"), LV_ALIGN_TOP_LEFT, 0, 0);

    // Green = quiet window, amber = moving, grey = no data.
    s_live_dot = lv_obj_create(live);
    lv_obj_set_size(s_live_dot, 10, 10);
    lv_obj_set_style_radius(s_live_dot, 5, 0);
    lv_obj_set_style_bg_color(s_live_dot, COL_FAINT, 0);
    lv_obj_set_style_bg_opa(s_live_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_live_dot, 0, 0);
    lv_obj_clear_flag(s_live_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_live_dot, LV_ALIGN_TOP_RIGHT, 0, 2);

    s_live_counts = theme_label(live, "--", FONT_XL, COL_TEXT);
    lv_obj_align(s_live_counts, LV_ALIGN_TOP_LEFT, 0, 22);

    s_live_kg = theme_label(live, "", FONT_MD, COL_BLUE);
    lv_obj_align(s_live_kg, LV_ALIGN_TOP_LEFT, 0, 74);
    s_live_rig = theme_label(live, "", FONT_XS, COL_MUTED);
    lv_obj_align(s_live_rig, LV_ALIGN_TOP_RIGHT, 0, 78);

    lv_obj_align(theme_separator(live, IW), LV_ALIGN_TOP_LEFT, 0, 108);
    lv_obj_align(w_caption(live, "NOISE  -  5 s WINDOW"), LV_ALIGN_TOP_LEFT, 0, 120);
    s_win_cap = theme_label(live, "", FONT_XS, COL_FAINT);
    lv_obj_align(s_win_cap, LV_ALIGN_TOP_RIGHT, 0, 120);

    s_win_minmax = w_kv_row(live, "Min / max",    144, IW);
    s_win_p2p    = w_kv_row(live, "Peak-to-peak", 170, IW);
    s_win_sd     = w_kv_row(live, "Std dev",      196, IW);
    s_win_n      = w_kv_row(live, "Samples",      222, IW);

    // ---- Tare row ----------------------------------------------------------
    lv_obj_align(theme_separator(live, IW), LV_ALIGN_TOP_LEFT, 0, 250);
    s_tare_lbl = theme_label(live, "Tare  --", FONT_SM, COL_MUTED);
    lv_obj_align(s_tare_lbl, LV_ALIGN_TOP_LEFT, 0, 271);

    lv_obj_t* tclr = w_button(live, "Clear", WBTN_GHOST, FONT_SM, tare_clear_cb, nullptr);
    lv_obj_set_size(tclr, 76, 36);
    lv_obj_align(tclr, LV_ALIGN_TOP_RIGHT, 0, 262);

    lv_obj_t* tz = w_button(live, LV_SYMBOL_REFRESH "  Zero", WBTN_PRIMARY, FONT_SM, tare_cb, nullptr);
    lv_obj_set_size(tz, 104, 36);
    lv_obj_align(tz, LV_ALIGN_TOP_RIGHT, -84, 262);

    // ================= RIGHT: 5 s captures ===============================
    lv_obj_t* card = theme_card(scr);
    lv_obj_set_size(card, RIGHT_W, TOP_H);
    lv_obj_set_pos(card, RIGHT_X, CONTENT_Y);

    lv_obj_align(w_caption(card, "CAPTURE  -  5 s AVERAGE"), LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* rst = w_button(card, "Clear", WBTN_GHOST, FONT_XS, clear_cb, nullptr);
    lv_obj_set_size(rst, 80, 30);
    lv_obj_align(rst, LV_ALIGN_TOP_RIGHT, 0, -6);

    s_cap_status = theme_label(card, "Ready", FONT_XS, COL_MUTED);
    lv_obj_set_width(s_cap_status, IW);
    lv_label_set_long_mode(s_cap_status, LV_LABEL_LONG_DOT);
    lv_obj_align(s_cap_status, LV_ALIGN_TOP_LEFT, 0, 34);

    s_cap_bar = lv_bar_create(card);
    lv_obj_set_size(s_cap_bar, IW, 6);
    lv_bar_set_range(s_cap_bar, 0, 100);
    lv_obj_set_style_bg_color(s_cap_bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_cap_bar, COL_BLUE, LV_PART_INDICATOR);
    lv_obj_align(s_cap_bar, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_add_flag(s_cap_bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_align(theme_separator(card, IW), LV_ALIGN_TOP_LEFT, 0, 70);

    // Column headers.
    lv_obj_t* hc = theme_label(card, "counts", FONT_XS, COL_FAINT);
    lv_obj_align(hc, LV_ALIGN_TOP_RIGHT, -(IW - COL_CNT), 80);
    lv_obj_align(theme_label(card, "kg", FONT_XS, COL_FAINT), LV_ALIGN_TOP_RIGHT, 0, 80);

    cap_row(card, 106, "Applied", COL_MUTED, &s_row_cnt[0], &s_row_kg[0]);
    cap_row(card, 140, "Removed", COL_MUTED, &s_row_cnt[1], &s_row_kg[1]);
    cap_row(card, 174, "Net",     COL_TEXT,  &s_row_cnt[2], &s_row_kg[2]);

    lv_obj_align(theme_separator(card, IW), LV_ALIGN_TOP_LEFT, 0, 210);

    // The constants in use, read-only (they live in the rig firmware).
    char b[64];
    snprintf(b, sizeof(b), "k = %.6e kg/count", RIG_CAL_SLOPE_KG_PER_COUNT);
    lv_obj_align(theme_label(card, b, FONT_XS, COL_MUTED), LV_ALIGN_TOP_LEFT, 0, 222);
    snprintf(b, sizeof(b), "c = %.4f kg  (plate-corrected)", RIG_CAL_INTERCEPT_KG);
    lv_obj_align(theme_label(card, b, FONT_XS, COL_MUTED), LV_ALIGN_TOP_LEFT, 0, 244);
    lv_obj_align(theme_label(card, "Set in the rig firmware - not editable here.",
                             FONT_XS, COL_FAINT), LV_ALIGN_TOP_LEFT, 0, 270);

    // ================= BOTTOM: capture buttons ===========================
    const int bw = (CONTENT_W - PAD_GAP) / 2;
    const int by = CONTENT_Y + TOP_H + PAD_GAP;

    s_btn_applied = w_button(scr, LV_SYMBOL_DOWNLOAD "  Capture APPLIED",
                             WBTN_PRIMARY, FONT_MD, applied_cb, nullptr);
    lv_obj_set_size(s_btn_applied, bw, BTN_H);
    lv_obj_set_pos(s_btn_applied, PAD_EDGE, by);

    s_btn_removed = w_button(scr, LV_SYMBOL_UP "  Capture REMOVED",
                             WBTN_SECONDARY, FONT_MD, removed_cb, nullptr);
    lv_obj_set_size(s_btn_removed, bw, BTN_H);
    lv_obj_set_pos(s_btn_removed, PAD_EDGE + bw + PAD_GAP, by);

    paint_captures();
    return scr;
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz)
// ---------------------------------------------------------------------------
void screen_calibration_refresh() {
    if (!s_live_counts) return;
    char b[80];
    const uint32_t now = millis();

    // The link may come up after the screen was opened: raise the rate then.
    if (!s_raised_rate && g_app.link_ok && g_app.rig != RigState::Running) {
        link_cmd_telemetry_hz(CAL_TELEM_HZ);
        s_raised_rate = true;
    }
    if (!g_app.link_ok) s_raised_rate = false;    // re-send after reconnect

    // ---- No data: say why, never show a stale number ---------------------
    if (!g_app.link_ok || !g_app.has_counts) {
        set_cell(s_live_counts, "--", COL_FAINT);
        lv_label_set_text(s_live_kg, "");
        lv_label_set_text(s_live_rig, g_app.link_ok ? "needs rig firmware rev 6.1" : "no link to rig");
        lv_obj_set_style_bg_color(s_live_dot, COL_FAINT, 0);
        lv_label_set_text(s_win_minmax, "--");
        lv_label_set_text(s_win_p2p, "--");
        lv_label_set_text(s_win_sd, "--");
        lv_label_set_text(s_win_n, "--");
        lv_label_set_text(s_win_cap, "");
        set_cell(s_tare_lbl, "Tare  --", COL_FAINT);
        if (s_capturing != CAP_NONE) {
            s_capturing = CAP_NONE;
            lv_obj_add_flag(s_cap_bar, LV_OBJ_FLAG_HIDDEN);
            set_cell(s_cap_status, "Capture aborted: no data", COL_RED);
        }
        w_button_enable(s_btn_applied, false);
        w_button_enable(s_btn_removed, false);
        return;
    }

    const bool busy = (s_capturing != CAP_NONE);
    w_button_enable(s_btn_applied, !busy);
    w_button_enable(s_btn_removed, !busy);

    // ---- Live counts + kg -------------------------------------------------
    const int32_t c = g_app.counts;
    const bool sat = (c >= 8388607L || c <= -8388608L);
    snprintf(b, sizeof(b), "%ld", (long)c);
    set_cell(s_live_counts, b, sat ? COL_RED : COL_TEXT);

    if (sat) {
        set_cell(s_live_kg, "ADC SATURATED", COL_RED);
    } else {
        snprintf(b, sizeof(b), "%.3f kg", counts_to_kg(c));
        set_cell(s_live_kg, b, COL_BLUE);
    }
    snprintf(b, sizeof(b), "rig (filtered) %.2f kg", g_app.force_kg);
    lv_label_set_text(s_live_rig, b);

    // ---- Noise window -----------------------------------------------------
    // Reset every WINDOW_MS so an old spike does not dominate forever, but
    // keep showing the last complete window's figures until the next one has
    // a few samples (otherwise the rows would flicker to zero every 5 s).
    if (now - s_win_start >= WINDOW_MS) {
        g_app.cnt_window.reset();
        s_win_start = now;
    }
    const CountStats& w = g_app.cnt_window;
    if (w.n >= 2) {
        const double p2p_kg = w.p2p() * RIG_CAL_SLOPE_KG_PER_COUNT;
        snprintf(b, sizeof(b), "%ld / %ld", (long)w.mn, (long)w.mx);
        lv_label_set_text(s_win_minmax, b);
        snprintf(b, sizeof(b), "%ld  (%.3f kg)", (long)w.p2p(), p2p_kg);
        set_cell(s_win_p2p, b, p2p_kg > NOISY_P2P_KG ? COL_ORANGE : COL_TEXT);
        snprintf(b, sizeof(b), "%.1f  (%.4f kg)", w.sd(), w.sd() * RIG_CAL_SLOPE_KG_PER_COUNT);
        lv_label_set_text(s_win_sd, b);
        snprintf(b, sizeof(b), "%lu  @ %d Hz", (unsigned long)w.n,
                 s_raised_rate ? CAL_TELEM_HZ : IDLE_TELEM_HZ);
        lv_label_set_text(s_win_n, b);
        lv_obj_set_style_bg_color(s_live_dot, p2p_kg > NOISY_P2P_KG ? COL_ORANGE : COL_GREEN, 0);
    }
    snprintf(b, sizeof(b), "%lu s", (unsigned long)((now - s_win_start) / 1000));
    lv_label_set_text(s_win_cap, b);

    // ---- Tare -------------------------------------------------------------
    if (g_app.has_tare) {
        snprintf(b, sizeof(b), "Tare  %.3f kg", g_app.tare_kg);
        set_cell(s_tare_lbl, b, fabsf(g_app.tare_kg) > 0.0005f ? COL_TEXT : COL_MUTED);
    } else {
        set_cell(s_tare_lbl, "Tare  -- (rig rev 6.2)", COL_FAINT);
    }

    // ---- Capture in progress ---------------------------------------------
    if (busy) {
        const uint32_t el = now - s_cap_start;
        if (el >= CAPTURE_MS) { finish_capture(); return; }
        lv_bar_set_value(s_cap_bar, (int)(el * 100 / CAPTURE_MS), LV_ANIM_OFF);
        snprintf(b, sizeof(b), "Averaging %s - %lu s left, n=%lu",
                 s_capturing == CAP_APPLIED ? "APPLIED" : "REMOVED",
                 (unsigned long)((CAPTURE_MS - el + 999) / 1000),
                 (unsigned long)g_app.cnt_capture.n);
        set_cell(s_cap_status, b, COL_BLUE);
    }
}
