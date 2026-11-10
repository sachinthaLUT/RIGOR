#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "../app/state.h"
#include "../link/link.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  STEPPED LOADING
//
//  Top    : three profile slots + Save / Clear
//  Left   : the 10-step table of the working profile, live force in its header
//  Right  : step editor (with the step count and total time in its header),
//           sequencer status, EXECUTE / STOP
//
//  ---------------------------------------------------------------------
//  THE SEQUENCER  --  read this before changing anything below
//  ---------------------------------------------------------------------
//  The rig firmware has NO sequencer. It understands one setpoint at a time
//  ("H30"). So the HMI drives the sequence itself:
//
//      send H<load>  ->  wait for the force to arrive  ->  hold for N s
//      ->  next step  ->  ... ->  stop + retract at the end
//
//  STEP TYPES (10/2026)
//    load > 0, hold > 0   LOAD : ramp to the load, hold it for `hold` s
//    load = 0, hold > 0   REST : stop, retract ~5 mm to unload the specimen,
//                                wait `hold` s, then the next step pushes
//                                again from free air
//    hold = 0             SKIP : ignored when the sequence runs
//
//  REST retracts the rig's fixed ~5 mm ('B') and then waits; it does NOT
//  drive the actuator backwards for the whole rest time, which would run it
//  into the retract end stop on any rest longer than a second or two.
//
//  This board is responsible for a rig that is under load. Three
//  consequences are designed in:
//
//   1. LINK LOSS ABORTS. If telemetry stops for SEQ_LINK_ABORT_MS we send S
//      and stop the sequence. Without this, a knocked cable leaves the rig
//      holding its last setpoint indefinitely with nothing watching it.
//
//   2. FAULT ABORTS. If the rig latches a fault mid-sequence we stop
//      immediately rather than queueing more setpoints at a dead motor.
//
//   3. THE HOLD TIMER STARTS AT ARRIVAL, not at the moment we sent the
//      command. A step that says "40 kg for 10 s" means ten seconds AT
//      40 kg. (A REST timer starts when the retract is sent.)
//
//  Leaving the screen does NOT stop a running sequence -- the timer is
//  global. The STOP button and Home's STOP both end it.
// ===========================================================================

// ---- Sequencer tuning ------------------------------------------------------
static const uint32_t SEQ_TICK_MS        = 100;    // sequencer update period
static const uint32_t SEQ_LINK_ABORT_MS  = 1500;   // no telemetry -> abort
static const uint32_t SEQ_ARRIVE_TIMEOUT = 60000;  // never reached target -> abort

// ---- Layout ---------------------------------------------------------------
static const int PROF_H   = 46;                       // profile bar height
static const int BAR_Y    = TOPBAR_H + 6;
static const int TBL_Y    = BAR_Y + PROF_H + 10;
static const int JOG_H    = 54;                       // Jog / Retract row
static const int TBL_H    = SCREEN_H - TBL_Y - PAD_EDGE - JOG_H - 10;
static const int EDIT_H   = SCREEN_H - TBL_Y - PAD_EDGE;   // editor is full height
static const int TBL_W    = 520;
static const int EDIT_X   = PAD_EDGE + TBL_W + 10;
static const int EDIT_W   = SCREEN_W - EDIT_X - PAD_EDGE;

// Editor panel, inner coordinates (card padding 12). Everything above the
// buttons is laid out from the top, the buttons from the bottom, and the two
// never meet -- the old totals line sat ON the EXECUTE button.
static const int ED_LOAD_Y   = 30;    // "Load" caption; stepper 16 px below
static const int ED_HOLD_Y   = 92;    // "Hold time" caption
static const int ED_HINT_Y   = 156;   // what this step will do
static const int ED_SEP_Y    = 180;
static const int ED_STATUS_Y = 188;   // sequencer status, up to two lines
static const int ED_EXEC_H   = 48;
static const int ED_STOP_H   = 44;

// ---- Widget handles --------------------------------------------------------
static lv_obj_t* s_prof_btn[PROFILE_COUNT]  = { nullptr };
static lv_obj_t* s_prof_name[PROFILE_COUNT] = { nullptr };
static lv_obj_t* s_prof_sub[PROFILE_COUNT]  = { nullptr };
static lv_obj_t* s_prof_dot[PROFILE_COUNT]  = { nullptr };  // unsaved marker

static lv_obj_t* s_row_bg[PROFILE_STEPS]    = { nullptr };   // the cell
static lv_obj_t* s_row_badge[PROFILE_STEPS] = { nullptr };   // number badge
static lv_obj_t* s_row_num[PROFILE_STEPS]   = { nullptr };
static lv_obj_t* s_row_bar[PROFILE_STEPS]   = { nullptr };
static lv_obj_t* s_row_load[PROFILE_STEPS]  = { nullptr };
static lv_obj_t* s_row_unit[PROFILE_STEPS]  = { nullptr };
static lv_obj_t* s_row_hold[PROFILE_STEPS]  = { nullptr };

// Cell width, set when the grid is built; the load bar scales against it.
static int GRID_CELL_W = 240;

static lv_obj_t* s_edit_title = nullptr;
static lv_obj_t* s_totals     = nullptr;   // "8 steps, 15 s" in the header
static lv_obj_t* s_load_val   = nullptr;
static lv_obj_t* s_hold_val   = nullptr;
static lv_obj_t* s_hint       = nullptr;   // LOAD / REST / SKIP explanation
static lv_obj_t* s_exec_btn   = nullptr;
static lv_obj_t* s_stop_btn   = nullptr;
static lv_obj_t* s_status     = nullptr;   // sequencer progress line
static lv_obj_t* s_live       = nullptr;   // live force, table header

// ---- Editor / sequencer state ----------------------------------------------
static int s_sel_step = 0;                 // which table row is being edited

enum class SeqPhase : uint8_t { Idle, Ramping, Holding, Resting };
static SeqPhase    s_phase       = SeqPhase::Idle;
static int         s_seq_step    = 0;
static uint32_t    s_phase_start = 0;      // millis() when this phase began
static lv_timer_t* s_seq_timer   = nullptr;

static void seq_stop(const char* why, lv_color_t col);

// ===========================================================================
//  STEP CLASSIFICATION
// ===========================================================================
static inline bool step_runs(const ProfileStep& s) { return s.hold_s > 0; }
static inline bool step_rest(const ProfileStep& s) { return s.hold_s > 0 && s.load_kg <= 0.0f; }

static const Profile& view(int idx) {
    return (idx == g_profile_active) ? g_working : g_profiles[idx];
}

// Steps that will actually execute (skips excluded), and how many are rests.
static int runnable_count(int idx, int* rests = nullptr) {
    const Profile& p = view(idx);
    const int n = profile_step_count(idx);
    int c = 0, r = 0;
    for (int i = 0; i < n; i++) {
        if (step_runs(p.step[i])) { c++; if (step_rest(p.step[i])) r++; }
    }
    if (rests) *rests = r;
    return c;
}

// ===========================================================================
//  PAINTING
// ===========================================================================
static void paint_profiles() {
    for (int i = 0; i < PROFILE_COUNT; i++) {
        if (!s_prof_btn[i]) continue;
        const bool active = (i == g_profile_active);
        const int  n      = runnable_count(i);
        const bool empty  = (profile_step_count(i) == 0);

        // Active = solid blue. Saved = inset. Empty = dimmer than saved, so a
        // never-used slot is not mistaken for one holding a sequence.
        lv_obj_set_style_bg_color(s_prof_btn[i],
            active ? COL_BLUE : (empty ? COL_BG : COL_CARD2), 0);
        lv_obj_set_style_border_color(s_prof_btn[i],
            active ? COL_BLUE : (empty ? COL_LINE : COL_CARD3), 0);
        lv_obj_set_style_border_width(s_prof_btn[i], active ? 0 : 2, 0);

        lv_obj_set_style_text_color(s_prof_name[i],
            active ? COL_TEXT : (empty ? COL_FAINT : COL_TEXT), 0);

        char sub[28];
        if (empty) snprintf(sub, sizeof(sub), "Empty");
        else       snprintf(sub, sizeof(sub), "%d steps - %lu s",
                            n, (unsigned long)profile_total_seconds(i));
        lv_label_set_text(s_prof_sub[i], sub);
        lv_obj_set_style_text_color(s_prof_sub[i],
            active ? COL_TEXT : COL_FAINT, 0);
        lv_obj_set_style_text_opa(s_prof_sub[i], active ? LV_OPA_80 : LV_OPA_COVER, 0);

        // Amber dot = unsaved edits in this slot. Without it there is no way
        // to tell an edited profile from a saved one, and tapping another
        // slot would quietly discard the work.
        if (active && g_profile_dirty) lv_obj_clear_flag(s_prof_dot[i], LV_OBJ_FLAG_HIDDEN);
        else                           lv_obj_add_flag(s_prof_dot[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void paint_table() {
    const int n_used = profile_step_count(g_profile_active);

    for (int i = 0; i < PROFILE_STEPS; i++) {
        if (!s_row_bg[i]) continue;

        const bool sel     = (i == s_sel_step);
        const bool running = (s_phase != SeqPhase::Idle && i == s_seq_step);
        const ProfileStep& st = g_working.step[i];
        const bool used    = (st.load_kg > 0.0f || st.hold_s > 0);
        const bool rest    = step_rest(st);
        const bool skip    = used && !step_runs(st) && i < n_used;

        // The EXECUTING step outranks the one being edited: during a run,
        // "where is it now" is what the operator needs to see.
        lv_color_t accent;
        if (running) {
            accent = COL_GREEN;
            lv_obj_set_style_bg_color(s_row_bg[i], COL_GREEN_BG, 0);
            lv_obj_set_style_border_color(s_row_bg[i], COL_GREEN, 0);
            lv_obj_set_style_border_width(s_row_bg[i], 2, 0);
        } else if (sel) {
            accent = COL_BLUE;
            lv_obj_set_style_bg_color(s_row_bg[i], COL_BLUE_BG, 0);
            lv_obj_set_style_border_color(s_row_bg[i], COL_BLUE, 0);
            lv_obj_set_style_border_width(s_row_bg[i], 2, 0);
        } else if (used) {
            accent = COL_CARD3;
            lv_obj_set_style_bg_color(s_row_bg[i], COL_CARD2, 0);
            lv_obj_set_style_border_width(s_row_bg[i], 0, 0);
        } else {
            // Unused cells sit darker with a hairline, so the end of the
            // sequence is obvious at a glance.
            accent = COL_CARD3;
            lv_obj_set_style_bg_color(s_row_bg[i], COL_BG, 0);
            lv_obj_set_style_border_color(s_row_bg[i], COL_LINE, 0);
            lv_obj_set_style_border_width(s_row_bg[i], 1, 0);
        }
        lv_obj_set_style_bg_opa(s_row_bg[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_row_badge[i], accent, 0);

        char b[12];
        if (rest) {
            lv_label_set_text(s_row_load[i], "Rest");
            lv_obj_add_flag(s_row_unit[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            snprintf(b, sizeof(b), "%.0f", st.load_kg);
            lv_label_set_text(s_row_load[i], b);
            lv_obj_clear_flag(s_row_unit[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (skip) lv_label_set_text(s_row_hold[i], "skip");
        else {
            snprintf(b, sizeof(b), "%u s", (unsigned)st.hold_s);
            lv_label_set_text(s_row_hold[i], b);
        }

        const lv_color_t tc = (used && !skip) ? COL_TEXT : COL_FAINT;
        lv_obj_set_style_text_color(s_row_load[i], rest ? COL_BLUE : tc, 0);
        lv_obj_set_style_text_color(s_row_hold[i], skip ? COL_ORANGE : tc, 0);
        lv_obj_set_style_text_color(s_row_unit[i], (used && !skip) ? COL_MUTED : COL_FAINT, 0);
        lv_obj_set_style_text_color(s_row_num[i],  used || sel ? COL_TEXT : COL_FAINT, 0);

        // Re-anchor the unit: "5" and "50" are different widths.
        lv_obj_align_to(s_row_unit[i], s_row_load[i], LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);

        // Load bar as a fraction of the rig ceiling.
        int w = (int)((GRID_CELL_W - 16) * (st.load_kg / RIG_MAX_KG));
        if (st.load_kg > 0.0f && w < 4) w = 4;
        if (w < 0) w = 0;
        lv_obj_set_width(s_row_bar[i], w);
        lv_obj_set_style_bg_color(s_row_bar[i],
            running ? COL_GREEN : (sel ? COL_BLUE : COL_BLUE_DK), 0);
    }
}

static void paint_editor() {
    char b[40];
    const ProfileStep& st = g_working.step[s_sel_step];

    snprintf(b, sizeof(b), "Step %d", s_sel_step + 1);
    lv_label_set_text(s_edit_title, b);

    // Totals live in the header now, right-aligned beside the title.
    const int n = runnable_count(g_profile_active);
    snprintf(b, sizeof(b), "%d step%s, %lu s", n, n == 1 ? "" : "s",
             (unsigned long)profile_total_seconds(g_profile_active));
    lv_label_set_text(s_totals, b);

    if (step_rest(st)) lv_label_set_text(s_load_val, "Rest");
    else { snprintf(b, sizeof(b), "%.0f kg", st.load_kg); lv_label_set_text(s_load_val, b); }

    snprintf(b, sizeof(b), "%u s", (unsigned)st.hold_s);
    lv_label_set_text(s_hold_val, b);

    // One line saying what this step will do when the sequence runs.
    if (st.hold_s == 0 && st.load_kg <= 0.0f) {
        lv_label_set_text(s_hint, "0 kg = rest,  0 s = skip");
        lv_obj_set_style_text_color(s_hint, COL_FAINT, 0);
    } else if (st.hold_s == 0) {
        lv_label_set_text(s_hint, "0 s hold: step is skipped");
        lv_obj_set_style_text_color(s_hint, COL_ORANGE, 0);
    } else if (st.load_kg <= 0.0f) {
        lv_label_set_text(s_hint, "Rest: retract 5 mm, then wait");
        lv_obj_set_style_text_color(s_hint, COL_BLUE, 0);
    } else {
        lv_label_set_text(s_hint, "0 kg = rest,  0 s = skip");
        lv_obj_set_style_text_color(s_hint, COL_FAINT, 0);
    }
}

static void paint_all() { paint_profiles(); paint_table(); paint_editor(); }

// ===========================================================================
//  SEQUENCER
// ===========================================================================
static void seq_set_status(const char* txt, lv_color_t col) {
    if (!s_status) return;
    lv_label_set_text(s_status, txt);
    lv_obj_set_style_text_color(s_status, col, 0);
}

static void seq_stop(const char* why, lv_color_t col) {
    if (s_seq_timer) { lv_timer_del(s_seq_timer); s_seq_timer = nullptr; }
    s_phase = SeqPhase::Idle;

    // ALWAYS stop, whatever the reason -- and retract, so a finished or
    // aborted sequence never leaves the specimen loaded.
    link_cmd_stop_and_retract();

    seq_set_status(why, col);
    if (s_exec_btn) w_button_enable(s_exec_btn, true);
    paint_table();
}

// Start step `from`, or the next one after it that runs (skips 0 s holds).
// Finishes the sequence when none is left. Returns false if it stopped.
static bool seq_begin_from(int from) {
    const int n = profile_step_count(g_profile_active);
    int i = from;
    while (i < n && !step_runs(g_working.step[i])) i++;

    if (i >= n) {
        seq_stop(LV_SYMBOL_OK "  Sequence complete", COL_GREEN);
        w_toast("Sequence complete", COL_GREEN, 2500);
        return false;
    }

    s_seq_step    = i;
    s_phase_start = millis();

    if (step_rest(g_working.step[i])) {
        // Unload and wait. The rest timer runs from here: the retract itself
        // takes about a second and is part of the rest.
        link_cmd_stop_and_retract();
        s_phase = SeqPhase::Resting;
    } else {
        // After a rest the rig is idle, so this is a NEW run and may be
        // refused (e-stop pressed, driver too hot) -- link_cmd_hold says why.
        if (!link_cmd_hold(g_working.step[i].load_kg)) {
            seq_stop(LV_SYMBOL_WARNING "  Aborted: rig refused the step", COL_RED);
            return false;
        }
        s_phase = SeqPhase::Ramping;
    }
    paint_table();
    return true;
}

static void seq_tick(lv_timer_t* /*t*/) {
    if (s_phase == SeqPhase::Idle) return;

    // ---- Abort conditions, checked before anything else ------------------
    if (!g_app.link_ok || link_age_ms() > SEQ_LINK_ABORT_MS) {
        seq_stop(LV_SYMBOL_WARNING "  Aborted: link lost", COL_RED);
        w_modal("Sequence aborted",
                "The link to the rig dropped mid-sequence.\n\n"
                "A stop was sent, but confirm the rig is unloaded.",
                COL_RED, "OK", nullptr, nullptr);
        return;
    }
    if (g_app.rig == RigState::Fault) {
        seq_stop(LV_SYMBOL_WARNING "  Aborted: rig fault", COL_RED);
        return;                      // the global fault modal explains why
    }

    const ProfileStep& st = g_working.step[s_seq_step];
    const int      n       = profile_step_count(g_profile_active);
    const uint32_t elapsed = millis() - s_phase_start;
    const uint32_t hold_ms = (uint32_t)st.hold_s * 1000UL;
    char msg[72];

    if (s_phase == SeqPhase::Ramping) {
        // "Arrived" = inside the rig's own deadband. Using the same figure
        // means the UI agrees with when the rig stops correcting.
        if (fabsf(g_app.force_kg - st.load_kg) <= RIG_DEADBAND_KG) {
            s_phase       = SeqPhase::Holding;
            s_phase_start = millis();   // hold clock starts ON ARRIVAL
            return;
        }
        if (elapsed > SEQ_ARRIVE_TIMEOUT) {
            seq_stop(LV_SYMBOL_WARNING "  Aborted: never reached target", COL_RED);
            return;
        }
        snprintf(msg, sizeof(msg), "Step %d/%d - ramping to %.0f kg",
                 s_seq_step + 1, n, st.load_kg);
        seq_set_status(msg, COL_ORANGE);
        return;
    }

    // ---- Holding or Resting: both are a countdown -------------------------
    if (elapsed >= hold_ms) { seq_begin_from(s_seq_step + 1); return; }

    const unsigned long left = (unsigned long)((hold_ms - elapsed + 999) / 1000);
    if (s_phase == SeqPhase::Resting) {
        snprintf(msg, sizeof(msg), "Step %d/%d - resting, %lus left",
                 s_seq_step + 1, n, left);
        seq_set_status(msg, COL_BLUE);
    } else {
        snprintf(msg, sizeof(msg), "Step %d/%d - holding %.0f kg, %lus left",
                 s_seq_step + 1, n, st.load_kg, left);
        seq_set_status(msg, COL_GREEN);
    }
}

static void seq_start() {
    seq_set_status("Starting...", COL_ORANGE);
    if (!seq_begin_from(0)) return;

    if (!s_seq_timer) s_seq_timer = lv_timer_create(seq_tick, SEQ_TICK_MS, nullptr);
    w_button_enable(s_exec_btn, false);
}

// ===========================================================================
//  CALLBACKS
// ===========================================================================
static int s_pending_profile = -1;      // slot awaiting a discard decision

static void switch_profile_now() {
    if (s_pending_profile < 0) return;
    profile_load_into_working(s_pending_profile);
    s_pending_profile = -1;
    s_sel_step = 0;
    paint_all();
}

static void profile_cb(lv_event_t* e) {
    if (s_phase != SeqPhase::Idle) {
        w_toast("Stop the sequence first", COL_ORANGE, 2000);
        return;
    }
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == g_profile_active) return;

    s_pending_profile = idx;

    // Unsaved edits would be silently destroyed by switching, so ask.
    if (g_profile_dirty) {
        w_modal("Discard changes?",
                "This profile has unsaved edits. Switching will lose them.",
                COL_ORANGE, "Discard", "Cancel", switch_profile_now);
        return;
    }
    switch_profile_now();
}

static void save_cb(lv_event_t* /*e*/) {
    if (s_phase != SeqPhase::Idle) { w_toast("Stop the sequence first", COL_ORANGE, 2000); return; }
    profile_save(g_profile_active);
    paint_profiles();
    char b[40];
    snprintf(b, sizeof(b), "Profile %c saved", 'A' + g_profile_active);
    w_toast(b, COL_GREEN, 2000);
}

static void clear_confirmed() {
    profile_clear_working();
    s_sel_step = 0;
    paint_all();
    w_toast("Steps cleared - Save to keep", COL_ORANGE, 2500);
}

static void clear_cb(lv_event_t* /*e*/) {
    if (s_phase != SeqPhase::Idle) { w_toast("Stop the sequence first", COL_ORANGE, 2000); return; }
    // Clear only touches the working copy; the stored profile survives until
    // Save is pressed. The dialog says so, because "Clear" alone reads as
    // permanent.
    w_modal("Clear all steps?",
            "Sets every step to zero.\n\n"
            "The saved profile is untouched until you press Save.",
            COL_ORANGE, "Clear", "Cancel", clear_confirmed);
}

static void row_cb(lv_event_t* e) {
    s_sel_step = (int)(intptr_t)lv_event_get_user_data(e);
    paint_table();
    paint_editor();
}

// ---- step editing ----------------------------------------------------------
// Editing the profile mid-sequence would change steps the sequencer is about
// to run, so the editor is locked while it runs.
static bool edit_guard() {
    if (s_phase == SeqPhase::Idle) return true;
    w_toast("Stop the sequence first", COL_ORANGE, 2000);
    return false;
}

static void mark_dirty() { g_profile_dirty = true; paint_all(); }

static void load_minus(lv_event_t*) {
    if (!edit_guard()) return;
    float v = g_working.step[s_sel_step].load_kg - 5.0f;
    if (v < 0) v = 0;
    g_working.step[s_sel_step].load_kg = v;
    mark_dirty();
}
static void load_plus(lv_event_t*) {
    if (!edit_guard()) return;
    float v = g_working.step[s_sel_step].load_kg + 5.0f;
    if (v > RIG_MAX_KG) v = RIG_MAX_KG;     // never offer what the rig refuses
    g_working.step[s_sel_step].load_kg = v;
    mark_dirty();
}
static void hold_minus(lv_event_t*) {
    if (!edit_guard()) return;
    int v = (int)g_working.step[s_sel_step].hold_s - 5;
    if (v < 0) v = 0;
    g_working.step[s_sel_step].hold_s = (uint16_t)v;
    mark_dirty();
}
static void hold_plus(lv_event_t*) {
    if (!edit_guard()) return;
    int v = (int)g_working.step[s_sel_step].hold_s + 5;
    if (v > 3600) v = 3600;
    g_working.step[s_sel_step].hold_s = (uint16_t)v;
    mark_dirty();
}

// Tap the value to type it instead of stepping to it.
static void load_typed(float v) { g_working.step[s_sel_step].load_kg = v; mark_dirty(); }
static void hold_typed(float v) { g_working.step[s_sel_step].hold_s = (uint16_t)v; mark_dirty(); }

static void load_kp_cb(lv_event_t*) {
    if (!edit_guard()) return;
    w_keypad("Step Load (0 = rest)", "kg", g_working.step[s_sel_step].load_kg,
             0.0f, RIG_MAX_KG, 0, load_typed);
}
static void hold_kp_cb(lv_event_t*) {
    if (!edit_guard()) return;
    w_keypad("Hold Time (0 = skip)", "s", g_working.step[s_sel_step].hold_s,
             0.0f, 3600.0f, 0, hold_typed);
}

// ---- jog / retract ---------------------------------------------------------
// Same three controls as the Static screen: positioning the fixture is just as
// necessary here, and walking to another screen to nudge the actuator is a
// pointless trip.
static bool jog_guard() {
    if (s_phase != SeqPhase::Idle) {
        w_toast("Stop the sequence first", COL_ORANGE, 2000);
        return false;
    }
    if (!g_app.link_ok) {
        w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000);
        return false;
    }
    return true;
}
static void jog_back_cb(lv_event_t*) {
    if (!jog_guard()) return;
    link_cmd_jog(-1);
    w_toast("Jog Retract ~1 mm", COL_BLUE, 1200);
}
static void jog_fwd_cb(lv_event_t*) {
    if (!jog_guard()) return;
    link_cmd_jog(+1);
    w_toast("Jog Extend ~1 mm", COL_BLUE, 1200);
}
static void retract_confirmed() {
    link_cmd_backoff();
    w_toast("Retracting ~5 mm", COL_BLUE, 2000);
}
static void retract_cb(lv_event_t*) {
    if (!jog_guard()) return;
    w_modal("Retract 5 mm?",
            "The actuator will retract about 5 mm to unload the cell.",
            COL_BLUE, "Retract", "Cancel", retract_confirmed);
}

// ---- execute / stop --------------------------------------------------------
static void exec_confirmed() { seq_start(); }

static void exec_cb(lv_event_t* /*e*/) {
    if (s_phase != SeqPhase::Idle) return;

    if (!g_app.link_ok) { w_toast(LV_SYMBOL_WARNING "  No link to rig", COL_ORANGE, 2000); return; }
    if (g_app.rig == RigState::Fault) {
        w_modal("Rig is faulted", "Clear the latched fault before running a sequence.",
                COL_RED, "Reset rig", "Cancel", link_cmd_reset);
        return;
    }

    int rests = 0;
    const int runs = runnable_count(g_profile_active, &rests);
    if (profile_step_count(g_profile_active) == 0) { w_toast("No steps to run", COL_ORANGE, 2000); return; }
    if (runs == 0) { w_toast("Every step has a 0 s hold", COL_ORANGE, 2500); return; }
    if (runs == rests) { w_toast("No loaded steps - only rests", COL_ORANGE, 2500); return; }

    // Find the peak so the confirmation states the worst case, not the first.
    const int n = profile_step_count(g_profile_active);
    float peak = 0;
    for (int i = 0; i < n; i++)
        if (step_runs(g_working.step[i]) && g_working.step[i].load_kg > peak)
            peak = g_working.step[i].load_kg;

    char rest_txt[24] = "";
    if (rests) snprintf(rest_txt, sizeof(rest_txt), " (%d rest%s)", rests, rests == 1 ? "" : "s");

    char msg[200];
    snprintf(msg, sizeof(msg),
             "Run %d steps%s, peak %.0f kg, %lu s total?\n\n"
             "The rig will be under load unattended. Check the fixture is clear.",
             runs, rest_txt, peak, (unsigned long)profile_total_seconds(g_profile_active));
    w_modal("Execute sequence", msg, COL_BLUE, "Execute", "Cancel", exec_confirmed);
}

static void stop_cb(lv_event_t* /*e*/) {
    if (s_phase != SeqPhase::Idle) {
        seq_stop("Stopped - retracting", COL_RED);   // seq_stop() retracts
    } else {
        // Not our sequence, but still worth unloading: a Static-screen hold
        // may be running.
        link_cmd_stop_and_retract();
    }
    w_toast("Stopped - retracting 5 mm", COL_RED, 2500);
}

// Transparent tap area over a stepper's value, opening the keypad.
static void value_hit(lv_obj_t* card, int w, int y, lv_event_cb_t cb) {
    lv_obj_t* hit = lv_obj_create(card);
    lv_obj_set_size(hit, w - 2 * 40 - 12, 40);
    lv_obj_align(hit, LV_ALIGN_TOP_LEFT, 40 + 6, y);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, nullptr);
}

// ===========================================================================
//  BUILD
// ===========================================================================
lv_obj_t* screen_stepped_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    theme_screen(scr);
    ui_topbar(scr, "Stepped Loading", true);

    // ---- Profile bar ------------------------------------------------------
    {
        const int bw = 160;
        for (int i = 0; i < PROFILE_COUNT; i++) {
            lv_obj_t* b = w_button(scr, "", WBTN_SECONDARY, FONT_SM,
                                   profile_cb, (void*)(intptr_t)i);
            lv_obj_set_size(b, bw, PROF_H);
            lv_obj_set_pos(b, PAD_EDGE + i * (bw + 10), BAR_Y);
            lv_obj_set_style_radius(b, RADIUS_CTRL, 0);

            char nm[16];
            snprintf(nm, sizeof(nm), "Profile %c", 'A' + i);
            s_prof_name[i] = lv_obj_get_child(b, 0);       // the empty label
            lv_label_set_text(s_prof_name[i], nm);
            lv_obj_align(s_prof_name[i], LV_ALIGN_CENTER, 0, -7);

            s_prof_sub[i] = theme_label(b, "Empty", FONT_XS, COL_FAINT);
            lv_obj_align(s_prof_sub[i], LV_ALIGN_CENTER, 0, 11);

            // Unsaved-changes dot, hidden unless dirty.
            s_prof_dot[i] = lv_obj_create(b);
            lv_obj_set_size(s_prof_dot[i], 8, 8);
            lv_obj_set_style_radius(s_prof_dot[i], 4, 0);
            lv_obj_set_style_bg_color(s_prof_dot[i], COL_ORANGE, 0);
            lv_obj_set_style_bg_opa(s_prof_dot[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(s_prof_dot[i], 0, 0);
            lv_obj_clear_flag(s_prof_dot[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(s_prof_dot[i], LV_ALIGN_TOP_RIGHT, -8, 8);
            lv_obj_add_flag(s_prof_dot[i], LV_OBJ_FLAG_HIDDEN);

            s_prof_btn[i] = b;
        }

        // Save / Clear, right-aligned.
        lv_obj_t* sv = w_button(scr, LV_SYMBOL_OK "  Save", WBTN_SECONDARY,
                                FONT_SM, save_cb, nullptr);
        lv_obj_set_size(sv, 112, PROF_H);
        lv_obj_set_pos(sv, SCREEN_W - PAD_EDGE - 2 * 112 - 10, BAR_Y);
        lv_obj_set_style_border_color(sv, COL_GREEN, 0);
        lv_obj_set_style_border_width(sv, 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(sv, 0), COL_GREEN, 0);

        lv_obj_t* cl = w_button(scr, "Clear", WBTN_GHOST, FONT_SM, clear_cb, nullptr);
        lv_obj_set_size(cl, 112, PROF_H);
        lv_obj_set_pos(cl, SCREEN_W - PAD_EDGE - 112, BAR_Y);
    }

    // ---- Step grid: 2 columns x 5 rows -----------------------------------
    // NOT a 10-row list. Ten stacked rows in this space gives ~25 px per row,
    // about half a fingertip. Two columns of five makes each cell ~243 x 48 --
    // five times the touch area, with nothing scrolled or hidden.
    {
        lv_obj_t* card = theme_card(scr);
        lv_obj_set_size(card, TBL_W, TBL_H);
        lv_obj_set_pos(card, PAD_EDGE, TBL_Y);
        lv_obj_set_style_pad_all(card, 12, 0);

        const int iw = TBL_W - 24;
        lv_obj_align(w_caption(card, "STEPS  -  TAP TO EDIT"), LV_ALIGN_TOP_LEFT, 0, 2);

        // Live force, right of the caption. Fixed width + right-aligned text:
        // a size-to-content label aligned TOP_RIGHT would grow off the card.
        s_live = theme_label(card, "--", FONT_MD, COL_FAINT);
        lv_obj_set_width(s_live, 260);
        lv_obj_set_style_text_align(s_live, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_live, LV_ALIGN_TOP_RIGHT, 0, -3);

        const int cw = (iw - PAD_GAP) / 2;      // cell width
        const int ch = (TBL_H - 24 - 30) / 5;   // cell pitch
        GRID_CELL_W = cw;                       // remembered for the load bar

        for (int i = 0; i < PROFILE_STEPS; i++) {
            const int col = i / 5, row = i % 5;
            const int cx  = col * (cw + PAD_GAP);
            const int cy  = 30 + row * ch;
            const int chh = ch - 4;             // leave a gap between cells

            lv_obj_t* cell = lv_obj_create(card);
            lv_obj_set_size(cell, cw, chh);
            lv_obj_align(cell, LV_ALIGN_TOP_LEFT, cx, cy);
            lv_obj_set_style_radius(cell, 9, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            s_row_bg[i] = cell;

            // Step-number badge.
            lv_obj_t* badge = lv_obj_create(cell);
            lv_obj_set_size(badge, 26, 26);
            lv_obj_set_style_radius(badge, 7, 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_set_style_pad_all(badge, 0, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(badge, LV_ALIGN_LEFT_MID, 8, 0);
            s_row_badge[i] = badge;

            char n[4];
            snprintf(n, sizeof(n), "%d", i + 1);
            s_row_num[i] = theme_label(badge, n, FONT_XS, COL_TEXT);
            lv_obj_center(s_row_num[i]);

            s_row_load[i] = theme_label(cell, "0", FONT_LG, COL_TEXT);
            lv_obj_align(s_row_load[i], LV_ALIGN_LEFT_MID, 44, -1);

            s_row_unit[i] = theme_label(cell, "kg", FONT_XS, COL_MUTED);
            lv_obj_align(s_row_unit[i], LV_ALIGN_LEFT_MID, 92, 4);

            s_row_hold[i] = theme_label(cell, "0 s", FONT_SM, COL_TEXT);
            lv_obj_align(s_row_hold[i], LV_ALIGN_RIGHT_MID, -10, 0);

            // Thin load bar along the bottom edge of the cell.
            lv_obj_t* bar = lv_obj_create(cell);
            lv_obj_set_size(bar, 0, 3);
            lv_obj_set_style_radius(bar, 2, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_pad_all(bar, 0, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 8, -5);
            s_row_bar[i] = bar;
        }
    }

    // ---- Jog / Retract row (under the step table) -------------------------
    {
        const int jy = TBL_Y + TBL_H + 10;
        const int jw = (TBL_W - 2 * PAD_GAP) / 3;
        struct JogDef { const char* top; const char* sub; lv_event_cb_t cb; };
        const JogDef defs[3] = {
            { LV_SYMBOL_MINUS "  Jog", "Retract 1mm", jog_back_cb },
            { LV_SYMBOL_PLUS  "  Jog", "Extend 1mm",  jog_fwd_cb  },
            { "Retract",               "5mm",         retract_cb  },
        };
        for (int i = 0; i < 3; i++) {
            lv_obj_t* b = w_button(scr, defs[i].top, WBTN_SECONDARY, FONT_SM,
                                   defs[i].cb, nullptr);
            lv_obj_set_size(b, jw, JOG_H);
            lv_obj_set_pos(b, PAD_EDGE + i * (jw + PAD_GAP), jy);
            lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_CENTER, 0, -9);
            lv_obj_align(theme_label(b, defs[i].sub, FONT_XS, COL_FAINT),
                         LV_ALIGN_CENTER, 0, 12);
        }
    }

    // ---- Editor panel -----------------------------------------------------
    {
        lv_obj_t* card = theme_card(scr);
        lv_obj_set_size(card, EDIT_W, EDIT_H);
        lv_obj_set_pos(card, EDIT_X, TBL_Y);
        lv_obj_set_style_pad_all(card, 12, 0);

        const int cw = EDIT_W - 24;

        // Header: "Step 3" on the left, "8 steps, 15 s" on the right.
        s_edit_title = theme_label(card, "Step 1", FONT_MD, COL_TEXT);
        lv_obj_align(s_edit_title, LV_ALIGN_TOP_LEFT, 0, 0);
        s_totals = theme_label(card, "", FONT_XS, COL_MUTED);
        lv_obj_align(s_totals, LV_ALIGN_TOP_RIGHT, 0, 4);

        // Load stepper. The centre value is itself a button -> keypad.
        lv_obj_align(theme_label(card, "Load", FONT_XS, COL_MUTED),
                     LV_ALIGN_TOP_LEFT, 0, ED_LOAD_Y);
        lv_obj_t* ls = w_stepper(card, cw, 40, load_minus, load_plus, nullptr, &s_load_val);
        lv_obj_align(ls, LV_ALIGN_TOP_LEFT, 0, ED_LOAD_Y + 16);
        value_hit(card, cw, ED_LOAD_Y + 16, load_kp_cb);

        lv_obj_align(theme_label(card, "Hold time", FONT_XS, COL_MUTED),
                     LV_ALIGN_TOP_LEFT, 0, ED_HOLD_Y);
        lv_obj_t* hs = w_stepper(card, cw, 40, hold_minus, hold_plus, nullptr, &s_hold_val);
        lv_obj_align(hs, LV_ALIGN_TOP_LEFT, 0, ED_HOLD_Y + 16);
        value_hit(card, cw, ED_HOLD_Y + 16, hold_kp_cb);

        s_hint = theme_label(card, "", FONT_XS, COL_FAINT);
        lv_obj_set_width(s_hint, cw);
        lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, ED_HINT_Y);

        lv_obj_align(theme_separator(card, cw), LV_ALIGN_TOP_LEFT, 0, ED_SEP_Y);

        s_status = theme_label(card, "Idle", FONT_XS, COL_MUTED);
        lv_obj_set_width(s_status, cw);
        lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, ED_STATUS_Y);

        s_exec_btn = w_button(card, LV_SYMBOL_PLAY "  EXECUTE", WBTN_SUCCESS,
                              FONT_MD, exec_cb, nullptr);
        lv_obj_set_size(s_exec_btn, cw, ED_EXEC_H);
        lv_obj_align(s_exec_btn, LV_ALIGN_BOTTOM_MID, 0, -(ED_STOP_H + 8));

        s_stop_btn = w_button(card, LV_SYMBOL_STOP "  STOP", WBTN_DANGER,
                              FONT_MD, stop_cb, nullptr);
        lv_obj_set_size(s_stop_btn, cw, ED_STOP_H);
        lv_obj_align(s_stop_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    paint_all();
    return scr;
}

// ---------------------------------------------------------------------------
//  LIVE REFRESH (~20 Hz)
//  The sequencer runs on its OWN timer so it keeps going when this screen is
//  not showing. This hook only keeps the visuals in step.
// ---------------------------------------------------------------------------
void screen_stepped_refresh() {
    if (!s_exec_btn) return;

    const bool seq_running = (s_phase != SeqPhase::Idle);
    w_button_enable(s_exec_btn, !seq_running && g_app.link_ok);

    if (!seq_running && !g_app.link_ok) seq_set_status("No link to rig", COL_ORANGE);

    // ---- Live force ---------------------------------------------------------
    // Dashes without a link, never the last value heard.
    if (g_app.link_ok) {
        char v[16], b[40];
        fmt_force(g_app.force_kg, v, sizeof(v));
        snprintf(b, sizeof(b), "Live  %s %s", v, unit_str());
        lv_label_set_text(s_live, b);
        lv_obj_set_style_text_color(s_live,
            seq_running && s_phase == SeqPhase::Holding ? COL_GREEN : COL_TEXT, 0);
    } else {
        lv_label_set_text(s_live, "Live  --");
        lv_obj_set_style_text_color(s_live, COL_FAINT, 0);
    }
}
