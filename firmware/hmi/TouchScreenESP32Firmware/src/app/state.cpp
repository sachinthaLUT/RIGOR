#include "state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

AppState g_app;
Settings g_settings;
Profile  g_profiles[PROFILE_COUNT];
Profile  g_working;
DynParams g_dyn;
int      g_profile_active = 0;
bool     g_profile_dirty  = false;

// ---------------------------------------------------------------------------
//  NVS
//
//  Namespace must be <= 15 chars (ESP-IDF limit).
//
//  SCHEMA VERSION: bumped whenever the on-flash layout of a profile changes.
//  If the stored version does not match, we ignore what is there and fall back
//  to defaults rather than reading a struct that no longer means what it did.
//  Without this, adding a field later would silently load garbage loads into a
//  machine that then applies them.
//
//  NOT bumped for "arestop": settings are stored as individual keys with
//  defaults, so a new key needs no version change -- and bumping would wipe
//  everyone's saved profiles and dynamic parameters for no reason.
// ---------------------------------------------------------------------------
static const char*    NVS_NS      = "forcetester";
// Bumped to 3: Settings gained auto_retract_on_loss and DynParams is new.
static const uint16_t NVS_VERSION = 3;
static Preferences s_prefs;

void state_init() {
    g_app = AppState{};
    settings_load();
    profiles_load();
    profile_load_into_working(0);
    dyn_load();
}

// ===========================================================================
//  SETTINGS
// ===========================================================================
void settings_defaults() { g_settings = Settings{}; }

void settings_load() {
    settings_defaults();
    if (!s_prefs.begin(NVS_NS, /*readOnly=*/true)) return;   // first boot

    g_settings.units          = (Units)s_prefs.getUChar("units",
                                    (uint8_t)g_settings.units);
    // Buzzer: the old key "buzz" was an on/off bool. Use it as the default
    // for the new 3-level key so an existing "off" stays off after updating.
    const bool old_on         = s_prefs.getBool ("buzz", true);
    g_settings.buzzer_level   = s_prefs.getUChar("buzlv", old_on ? 2 : 0);
    g_settings.confirm_high   = s_prefs.getBool ("cfmhi",  g_settings.confirm_high);
    g_settings.auto_retract_on_loss =
                                s_prefs.getBool ("arloss", g_settings.auto_retract_on_loss);
    g_settings.retract_after_estop =
                                s_prefs.getBool ("arestop", g_settings.retract_after_estop);
    s_prefs.end();

    // Clamp anything out of range (corrupt or hand-edited NVS).
    if (g_settings.buzzer_level > 2) g_settings.buzzer_level = 2;
}

void settings_save() {
    if (!s_prefs.begin(NVS_NS, false)) return;
    s_prefs.putUShort("ver",    NVS_VERSION);
    s_prefs.putUChar ("units",  (uint8_t)g_settings.units);
    s_prefs.putUChar ("buzlv",  g_settings.buzzer_level);
    s_prefs.putBool  ("cfmhi",  g_settings.confirm_high);
    s_prefs.putBool  ("arloss", g_settings.auto_retract_on_loss);
    s_prefs.putBool  ("arestop", g_settings.retract_after_estop);
    s_prefs.end();
}

// ===========================================================================
//  DYNAMIC TEST PARAMETERS
// ===========================================================================
static void dyn_sanitise() {
    if (g_dyn.bottom_kg < 0)          g_dyn.bottom_kg = 0;
    if (g_dyn.top_kg > RIG_MAX_KG)    g_dyn.top_kg    = RIG_MAX_KG;
    if (g_dyn.bottom_kg > g_dyn.top_kg) {
        // Bottom above top would invert the cycle. Swap rather than reject, so
        // a mis-typed pair still produces a sane test.
        float t = g_dyn.bottom_kg; g_dyn.bottom_kg = g_dyn.top_kg; g_dyn.top_kg = t;
    }
    if (g_dyn.bottom_s < 1)    g_dyn.bottom_s = 1;
    if (g_dyn.top_s    < 1)    g_dyn.top_s    = 1;
    if (g_dyn.bottom_s > 3600) g_dyn.bottom_s = 3600;
    if (g_dyn.top_s    > 3600) g_dyn.top_s    = 3600;
    if (g_dyn.cycles   < 1)    g_dyn.cycles   = 1;
    g_dyn.name[sizeof(g_dyn.name) - 1] = '\0';
}

void dyn_load() {
    g_dyn = DynParams{};
    if (!s_prefs.begin(NVS_NS, true)) return;
    if (s_prefs.getUShort("ver", 0) == NVS_VERSION) {
        DynParams tmp;
        if (s_prefs.getBytes("dyn", &tmp, sizeof(tmp)) == sizeof(tmp)) g_dyn = tmp;
    }
    s_prefs.end();
    dyn_sanitise();
}

void dyn_save() {
    dyn_sanitise();
    if (!s_prefs.begin(NVS_NS, false)) return;
    s_prefs.putUShort("ver", NVS_VERSION);
    s_prefs.putBytes("dyn", &g_dyn, sizeof(g_dyn));
    s_prefs.end();
}

uint32_t dyn_cycle_seconds() {
    // Dwells plus a rough allowance for the two ramps. The ramps are real time
    // the machine spends travelling, so ignoring them would make the estimated
    // duration meaningfully optimistic on a long fatigue run.
    const uint32_t RAMP_ALLOWANCE_S = 2;
    return (uint32_t)g_dyn.bottom_s + g_dyn.top_s + 2 * RAMP_ALLOWANCE_S;
}

// ===========================================================================
//  PROFILES
//
//  Each slot is one NVS blob: "prof0", "prof1", "prof2". Storing the whole
//  struct in a single key means a save is one flash write, and a half-written
//  profile is impossible.
// ===========================================================================
static void profile_key(int idx, char* buf, size_t n) {
    snprintf(buf, n, "prof%d", idx);
}

// Reject anything that would command the rig outside its limits. Applied on
// LOAD as well as save: NVS could have been written by an older build.
static void sanitise(Profile& p) {
    for (int i = 0; i < PROFILE_STEPS; i++) {
        if (p.step[i].load_kg < 0.0f)      p.step[i].load_kg = 0.0f;
        if (p.step[i].load_kg > RIG_MAX_KG) p.step[i].load_kg = RIG_MAX_KG;
        if (p.step[i].hold_s > 3600)       p.step[i].hold_s  = 3600;
    }
}

void profiles_load() {
    for (int i = 0; i < PROFILE_COUNT; i++) g_profiles[i] = Profile{};

    if (!s_prefs.begin(NVS_NS, true)) return;

    // Version gate: unknown layout -> leave every slot empty.
    if (s_prefs.getUShort("ver", 0) != NVS_VERSION) {
        s_prefs.end();
        return;
    }

    for (int i = 0; i < PROFILE_COUNT; i++) {
        char k[8];
        profile_key(i, k, sizeof(k));
        Profile tmp;
        size_t got = s_prefs.getBytes(k, &tmp, sizeof(tmp));
        if (got == sizeof(tmp)) {
            sanitise(tmp);
            g_profiles[i] = tmp;
        }
    }
    s_prefs.end();
}

void profile_save(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return;

    sanitise(g_working);
    g_working.used   = true;
    g_profiles[idx]  = g_working;

    if (!s_prefs.begin(NVS_NS, false)) return;
    char k[8];
    profile_key(idx, k, sizeof(k));
    s_prefs.putUShort("ver", NVS_VERSION);
    s_prefs.putBytes(k, &g_profiles[idx], sizeof(Profile));
    s_prefs.end();

    g_profile_dirty = false;
}

void profile_delete(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return;

    g_profiles[idx] = Profile{};          // empty and unused

    if (s_prefs.begin(NVS_NS, false)) {
        char k[8];
        profile_key(idx, k, sizeof(k));
        s_prefs.remove(k);
        s_prefs.end();
    }

    // If we just deleted what is on screen, clear the editor too, otherwise
    // the table would still show steps belonging to a slot that no longer
    // exists -- and Save would silently resurrect it.
    if (idx == g_profile_active) {
        g_working       = Profile{};
        g_profile_dirty = false;
    }
}

void profile_clear_working() {
    for (int i = 0; i < PROFILE_STEPS; i++) {
        g_working.step[i].load_kg = 0.0f;
        g_working.step[i].hold_s  = 0;
    }
    g_profile_dirty = true;               // not persisted until Save
}

void profile_load_into_working(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return;
    g_working        = g_profiles[idx];
    g_profile_active = idx;
    g_profile_dirty  = false;
}

// Read from the working copy for the slot being edited, so the profile
// buttons update live as steps are changed.
static const Profile& profile_view(int idx) {
    return (idx == g_profile_active) ? g_working : g_profiles[idx];
}

int profile_step_count(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return 0;
    const Profile& p = profile_view(idx);
    int last = 0;
    for (int i = 0; i < PROFILE_STEPS; i++) {
        if (p.step[i].load_kg > 0.0f || p.step[i].hold_s > 0) last = i + 1;
    }
    return last;
}

uint32_t profile_total_seconds(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return 0;
    const Profile& p = profile_view(idx);
    uint32_t total = 0;
    const int n = profile_step_count(idx);
    for (int i = 0; i < n; i++) total += p.step[i].hold_s;
    return total;
}

// ===========================================================================
//  UNIT FORMATTING
// ===========================================================================
const char* unit_str() {
    return (g_settings.units == Units::Newton) ? "N" : "kg";
}

const char* fmt_force(float kg, char* buf, unsigned buflen) {
    if (g_settings.units == Units::Newton) {
        snprintf(buf, buflen, "%.1f", kg_to_n(kg));   // N is ~10x bigger
    } else {
        snprintf(buf, buflen, "%.2f", kg);
    }
    return buf;
}
