#pragma once
#include <stdint.h>
#include <math.h>

// ===========================================================================
//  APPLICATION STATE
//
//  The ONLY place UI code reads rig data from. Screens never touch the serial
//  port; link.cpp fills these in, screens read them in their refresh() hook.
//
//  Everything here is touched only from the LVGL task (the link parser runs
//  on an lv_timer), so no mutex is needed.
//
//  UNITS: kilograms throughout. The rig works in kg, the load buttons are kg,
//  the calibration is kg. Newtons are a DISPLAY conversion only.
// ===========================================================================

// Mirrors the rig's Mode enum (MODE_IDLE / MODE_RUN / MODE_FAULT).
enum class RigState : uint8_t { Unknown, Idle, Running, Fault };

// Mirrors the rig's Regime enum (REG_APPROACH / REG_CONTACT).
enum class RigRegime : uint8_t { Unknown, Approach, Contact };

#define FAULT_TEXT_MAX 56

// Running statistics of raw load-cell counts (Calibration screen). link.cpp
// adds every N= value to BOTH accumulators in AppState; the Calibration
// screen resets them -- the window every 5 s, the capture when one starts.
struct CountStats {
    uint32_t n = 0;
    double   sum = 0.0, sumsq = 0.0;
    int32_t  mn = 0, mx = 0;
    bool     saturated = false;          // ADS1220 at +/- full scale

    void reset() { *this = CountStats{}; }
    void add(int32_t c) {
        if (n == 0) { mn = mx = c; }
        else { if (c < mn) mn = c; if (c > mx) mx = c; }
        sum += c; sumsq += (double)c * c; n++;
        if (c >= 8388607L || c <= -8388608L) saturated = true;
    }
    double  mean() const { return n ? sum / n : 0.0; }
    double  sd()   const {
        if (n < 2) return 0.0;
        const double m = mean(), v = (sumsq - n * m * m) / (n - 1);
        return v > 0 ? sqrt(v) : 0.0;
    }
    int32_t p2p()  const { return n ? mx - mn : 0; }
};

struct AppState {
    // ---- Live telemetry --------------------------------------------------
    float     force_kg    = 0.0f;   // filtered force from the load cell
    float     target_kg   = 0.0f;   // the rig's current setpoint
    int       pwm         = 0;      // applied signed PWM
    RigState  rig         = RigState::Unknown;
    RigRegime regime      = RigRegime::Unknown;
    uint8_t   limits      = 0;      // bit0 = push limit, bit1 = retract limit
    char      fault[FAULT_TEXT_MAX] = { 0 };

    // ---- Rig rev 5 (PCB rev 2) telemetry --------------------------------
    float     current_a      = 0.0f;   // motor current, + = push, - = retract
    float     current_pk_a   = 0.0f;   // largest |current| since the RIG booted
    float     drv_temp_c     = 0.0f;   // driver thermistor
    bool      drv_temp_ok    = false;  // false = sensor fault, or old rig firmware
    bool      estop          = false;  // e-stop switch open (pressed)
    bool      drv_fault      = false;  // G2 FLT pin low right now
    uint8_t   eot            = 0;      // end of travel: bit0 = extend, bit1 = retract
    bool      has_drive_data = false;  // rig sends I=/Td= (rev 5 or later)

    // ---- Rig rev 6.1 telemetry ------------------------------------------
    // Raw ADS1220 counts (20 ms average) for the Calibration screen.
    int32_t   counts      = 0;
    bool      has_counts  = false;     // rig sends N= (rev 6.1 or later)
    uint32_t  counts_seq  = 0;         // +1 per frame carrying N=
    CountStats cnt_window;             // rolling noise window
    CountStats cnt_capture;            // the 5 s capture average

    // ---- Rig rev 6.2 telemetry ------------------------------------------
    float     tare_kg     = 0.0f;      // zero offset the rig subtracts from F
    bool      has_tare    = false;     // rig sends tare= (rev 6.2 or later)

    // ---- Link health -----------------------------------------------------
    bool      link_ok     = false;
    uint32_t  last_rx_ms  = 0;
    uint32_t  frames_rx   = 0;
    uint32_t  bad_frames  = 0;

    // ---- UI-local run tracking ------------------------------------------
    float     requested_kg  = 0.0f;
    bool      run_requested = false;
};

extern AppState g_app;
void state_init();

// Mirrors DEADBAND_KG in the rig firmware -- KEEP THESE IN SYNC.
#define RIG_DEADBAND_KG 0.4f

// Mirrors MAX_SETPOINT_KG in the rig firmware. The rig refuses anything above
// this, so the UI must not offer it. KEEP IN SYNC.
#define RIG_MAX_KG      90.0f

// Mirror the rig's driver limits (TEMP_WARN_C, TEMP_TRIP_C, OC_TRIP_A).
// Used for colouring and for refusing a start; the rig enforces them itself.
// KEEP IN SYNC.
#define RIG_TEMP_WARN_C 70.0f
#define RIG_TEMP_TRIP_C 85.0f
#define RIG_I_TRIP_A     8.0f

// Mirror CAL_SLOPE_KG_PER_COUNT / CAL_INTERCEPT_KG in the rig firmware (UTM
// calibration 2026-10-15, intercept corrected for the 221.4 g plate).
// Used ONLY by the Calibration screen to turn counts into kg. KEEP IN SYNC.
#define RIG_CAL_SLOPE_KG_PER_COUNT  0.000058327734
#define RIG_CAL_INTERCEPT_KG        0.366767338

// ===========================================================================
//  USER SETTINGS  (persisted to NVS)
// ===========================================================================
enum class Units : uint8_t { Kilogram, Newton };

struct Settings {
    Units   units          = Units::Kilogram;

    // Rig buzzer volume: 0 = Off, 1 = Low, 2 = High. Applied ON THE RIG (it
    // owns the buzzer), sent with the 'V' command and re-sent on every
    // reconnect like 'W' and 'E'. Off also silences the rig's own alarms.
    // (Screen brightness was removed: the panel always runs at 100 %.)
    uint8_t buzzer_level   = 2;
    bool    confirm_high   = true;    // confirm before a high-load run

    // If the rig loses contact with this display mid-run:
    //   false = hold position (motor off, leadscrew self-locks)   [default]
    //   true  = retract ~5 mm to unload the specimen
    // The WATCHDOG runs on the rig, because it has to work when this board is
    // the thing that died. This flag is transmitted with the 'W' command and
    // re-sent on every reconnect, since a rebooted rig reverts to its default.
    bool    auto_retract_on_loss = false;

    // When the rig is reset after an emergency stop:
    //   true  = retract ~5 mm to unload the specimen   [default]
    //   false = clear the fault only, leave the actuator where it is
    // Sent with the 'E' command and re-sent on every reconnect, like 'W'.
    bool    retract_after_estop  = true;
};

extern Settings g_settings;

static inline bool buzzer_on() { return g_settings.buzzer_level != 0; }

void settings_defaults();
void settings_load();
void settings_save();

// ===========================================================================
//  DYNAMIC TEST PROFILES  (persisted to NVS)
//
//  Three named slots, each a sequence of up to PROFILE_STEPS steps. A step is
//  a target load plus how long to hold it.
//
//  A step with load == 0 AND hold == 0 is UNUSED, which is how a profile
//  shorter than 10 steps is represented. profile_step_count() returns the
//  index after the last used step, so trailing blanks are simply ignored.
//
//  When a sequence RUNS (screen_stepped.cpp):
//    hold == 0             -> step is SKIPPED (any load)
//    load == 0, hold > 0   -> REST: stop, retract ~5 mm to unload, wait
//                             hold s, then the next step pushes again
// ===========================================================================
#define PROFILE_COUNT 3
#define PROFILE_STEPS 10

struct ProfileStep {
    float    load_kg = 0.0f;
    uint16_t hold_s  = 0;
};

struct Profile {
    ProfileStep step[PROFILE_STEPS];
    bool        used = false;      // has this slot ever been saved?
};

extern Profile g_profiles[PROFILE_COUNT];

// The working copy the editor mutates. profile_save() copies it into a slot.
extern Profile g_working;

// Which slot is loaded into the editor, and whether it has unsaved edits.
extern int  g_profile_active;
extern bool g_profile_dirty;

void     profiles_load();               // read all slots from NVS (once, at boot)
void     profile_save(int idx);         // write working copy to a slot, clear dirty
void     profile_clear_working();       // zero the working copy (NOT saved yet)
void     profile_delete(int idx);       // erase a slot, mark it unused
void     profile_load_into_working(int idx);

// These operate on the WORKING copy when idx == g_profile_active, otherwise on
// the stored slot -- so the profile buttons can show live counts while editing.
int      profile_step_count(int idx);
uint32_t profile_total_seconds(int idx);

// ===========================================================================
//  DYNAMIC (CYCLIC) TEST PARAMETERS  -- persisted to NVS
//
//  Square wave only. The rig's force loop is tuned to SETTLE, not to TRACK a
//  moving setpoint, so it can hold two levels accurately but cannot follow a
//  sine or triangle without heavy lag and attenuation. A square wave asks it
//  to do exactly what it is good at; the result is trapezoidal in practice
//  (ramp, dwell, ramp, dwell), which is a standard fatigue-cycling profile.
// ===========================================================================
struct DynParams {
    float    bottom_kg   = 20.0f;
    float    top_kg      = 40.0f;
    uint16_t bottom_s    = 10;      // dwell at the bottom load
    uint16_t top_s       = 8;       // dwell at the top load
    uint32_t cycles      = 100;     // type a huge number for "continuous"
    char     name[33]    = "test1"; // becomes the CSV filename
};

extern DynParams g_dyn;
void dyn_load();
void dyn_save();

// Seconds for one full cycle (both dwells plus a rough ramp allowance).
uint32_t dyn_cycle_seconds();

// ===========================================================================
//  UNIT HELPERS
// ===========================================================================
static inline float kg_to_n(float kg) { return kg * 9.80665f; }
static inline float n_to_kg(float n)  { return n / 9.80665f; }

const char* fmt_force(float kg, char* buf, unsigned buflen);
const char* unit_str();

static inline float force_in_units(float kg) {
    return (g_settings.units == Units::Newton) ? kg_to_n(kg) : kg;
}

// Colour band for the driver temperature: 0 = normal, 1 = warning, 2 = trip.
static inline int drv_temp_level(float c) {
    return (c >= RIG_TEMP_TRIP_C) ? 2 : (c >= RIG_TEMP_WARN_C) ? 1 : 0;
}
