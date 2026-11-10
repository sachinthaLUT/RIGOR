#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "link.h"
#include "../app/state.h"
#include "../ui/widgets.h"

// ===========================================================================
//  WIRING  --  VERIFY AGAINST YOUR BOARD BEFORE FIRST RUN
//
//  Your panel config uses these GPIOs for the RGB bus:
//      HSYNC 39, VSYNC 41, DE 40, PCLK 42,
//      DATA  8,3,46,9,1,5,6,7,15,16,4,45,48,47,21,14
//      touch I2C 19/20, backlight 2
//
//  Cross-wire them: HMI TX -> RIG RX, HMI RX <- RIG TX, and share a ground.
//  Both boards are 3.3V logic, so no level shifter is needed.
//
//    HMI GPIO17 (TX) ----> RIG D0 (RX)
//    HMI GPIO18 (RX) <---- RIG D1 (TX)
//    GND             <---> GND
//
//  On the rig PCB this is connector J2: pin 3 = RX (rig), pin 4 = TX (rig).
//  J1 carries 5 V (pin 1) and GND (pin 4) to this display.
// ===========================================================================
static const int LINK_TX_PIN = 17;
static const int LINK_RX_PIN = 18;
static const uint32_t LINK_BAUD       = 115200;  // must match the rig firmware

// The rig sends telemetry every ~250 ms. Three missed frames = link down.
static const uint32_t LINK_TIMEOUT_MS = 1000;

// Heartbeat 'P' so the rig can tell "display connected but idle" from
// "display unplugged". Accepted silently by the rig.
static const uint32_t HEARTBEAT_MS = 1000;

// ---------------------------------------------------------------------------
//  RX LINE BUFFER
//  Rev 6.1 frames with a long fault reason reach ~160 characters, so the
//  buffer is 200 to leave margin. An over-long line is dropped, not split.
// ---------------------------------------------------------------------------
static char   s_buf[200];
static size_t s_len = 0;

// Set to 1 to dump everything arriving on Serial1 to the USB console.
#define LINK_DEBUG_RX 0

// ---------------------------------------------------------------------------
//  FIELD DECODERS
// ---------------------------------------------------------------------------
static RigState parse_state(const char* s) {
    if (!strcmp(s, "IDLE"))  return RigState::Idle;
    if (!strcmp(s, "RUN"))   return RigState::Running;
    if (!strcmp(s, "FAULT")) return RigState::Fault;
    return RigState::Unknown;
}

static RigRegime parse_regime(const char* s) {
    if (!strcmp(s, "APPR")) return RigRegime::Approach;
    if (!strcmp(s, "CONT")) return RigRegime::Contact;
    return RigRegime::Unknown;
}

// ---------------------------------------------------------------------------
//  LINE PARSER
//
//  Space-separated key=value tokens. UNKNOWN TOKENS ARE IGNORED, so the rig
//  can grow new fields without breaking this parser, and the rig's
//  human-readable chatter is skipped because it has no keys we recognise.
//
//  'flt' is cut off the end BEFORE tokenising, because its value has spaces.
// ---------------------------------------------------------------------------
static void handle_line(char* line) {
    bool got_data = false;

    char* flt = strstr(line, "flt=");
    if (flt) {
        *flt = '\0';
        const char* reason = flt + 4;
        strncpy(g_app.fault, reason, FAULT_TEXT_MAX - 1);
        g_app.fault[FAULT_TEXT_MAX - 1] = '\0';
    }

    char* save = nullptr;
    for (char* tok = strtok_r(line, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = tok;
        const char* val = eq + 1;

        if      (!strcmp(key, "F"))    { g_app.force_kg  = atof(val); got_data = true; }
        else if (!strcmp(key, "tgt"))  { g_app.target_kg = atof(val); got_data = true; }
        else if (!strcmp(key, "pwm"))  { g_app.pwm       = atoi(val); got_data = true; }
        else if (!strcmp(key, "lim"))  { g_app.limits    = (uint8_t)atoi(val); }
        else if (!strcmp(key, "reg"))  { g_app.regime    = parse_regime(val); got_data = true; }
        else if (!strcmp(key, "st"))   { g_app.rig       = parse_state(val);  got_data = true; }

        // ---- rev 5 fields ------------------------------------------------
        else if (!strcmp(key, "I"))    { g_app.current_a    = atof(val); g_app.has_drive_data = true; }
        else if (!strcmp(key, "Ipk"))  { g_app.current_pk_a = atof(val); }
        else if (!strcmp(key, "Td"))   {
            const float t = atof(val);
            g_app.drv_temp_ok = (t > -100.0f);       // rig sends -999 for a sensor fault
            if (g_app.drv_temp_ok) g_app.drv_temp_c = t;
        }
        else if (!strcmp(key, "es"))   { g_app.estop     = (atoi(val) != 0); }
        else if (!strcmp(key, "df"))   { g_app.drv_fault = (atoi(val) != 0); }
        else if (!strcmp(key, "eot"))  { g_app.eot       = (uint8_t)atoi(val); }

        // ---- rev 6.1 field -----------------------------------------------
        else if (!strcmp(key, "N"))    {
            g_app.counts     = (int32_t)atol(val);
            g_app.has_counts = true;
            g_app.counts_seq++;
            g_app.cnt_window.add(g_app.counts);
            g_app.cnt_capture.add(g_app.counts);
        }

        // ---- rev 6.2 field -----------------------------------------------
        else if (!strcmp(key, "tare")) { g_app.tare_kg = atof(val); g_app.has_tare = true; }
    }

    if (got_data) {
        g_app.link_ok    = true;
        g_app.last_rx_ms = millis();
        g_app.frames_rx++;
        if (g_app.rig != RigState::Fault) g_app.fault[0] = '\0';
    } else {
        g_app.bad_frames++;
    }
}

static void pump_rx() {
#if LINK_DEBUG_RX
    static uint32_t dbg_last = 0;
    if (millis() - dbg_last > 2000) {
        dbg_last = millis();
        Serial.printf("[link] alive, frames %lu ok / %lu bad\n",
                      (unsigned long)g_app.frames_rx,
                      (unsigned long)g_app.bad_frames);
    }
#endif

    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\r') continue;
        if (c == '\n') {
            s_buf[s_len] = '\0';
#if LINK_DEBUG_RX
            if (s_len > 0) Serial.printf("[link] RX line: \"%s\"\n", s_buf);
#endif
            if (s_len > 0) handle_line(s_buf);
            s_len = 0;
        } else if (s_len < sizeof(s_buf) - 1) {
            s_buf[s_len++] = c;
        } else {
            s_len = 0;
            g_app.bad_frames++;
        }
    }
}

// ===========================================================================
//  TX
// ===========================================================================
void link_send(const char* line) {
    Serial1.print(line);
    Serial1.print('\n');
}

bool link_cmd_hold(float kg) {
    // Only a NEW run is gated here. The rig enforces the same rules itself;
    // checking here as well means the operator gets told WHY, instead of the
    // rig silently staying idle.
    const bool new_run = (g_app.rig != RigState::Running);
    if (new_run && g_app.estop) {
        w_toast(LV_SYMBOL_WARNING "  E-stop pressed - release it first", COL_RED, 2500);
        return false;
    }
    if (new_run && g_app.has_drive_data && g_app.drv_temp_ok &&
        g_app.drv_temp_c >= RIG_TEMP_WARN_C) {
        char b[64];
        snprintf(b, sizeof(b), LV_SYMBOL_WARNING "  Driver at %.0f C - let it cool",
                 g_app.drv_temp_c);
        w_toast(b, COL_ORANGE, 3000);
        return false;
    }

    char out[16];
    snprintf(out, sizeof(out), "H%.1f", kg);
    link_send(out);

    g_app.requested_kg  = kg;
    g_app.run_requested = true;
    return true;
}

void link_cmd_stop()    { link_send("S"); g_app.run_requested = false; }

void link_cmd_stop_and_retract() {
    // STOP first, so if only one line survives a marginal link it is the one
    // that halts the motor.
    link_send("S");
    g_app.run_requested = false;

    // Skip the retract while faulted (the rig ignores motion then, and 'Z'
    // retracts by itself) or while the e-stop has motor power off.
    if (g_app.rig == RigState::Fault || g_app.estop) return;

    link_send("B");
}
void link_cmd_reset()   { link_send("Z"); }
void link_cmd_read()    { link_send("R"); }
void link_cmd_backoff() { link_send("B"); }
void link_cmd_jog(int dir) { link_send(dir > 0 ? "+" : "-"); }

void link_cmd_beep() { if (buzzer_on()) link_send("Q"); }

void link_cmd_loss_action(bool auto_retract) {
    link_send(auto_retract ? "W1" : "W0");
}

void link_cmd_estop_action(bool retract) {
    link_send(retract ? "E1" : "E0");
}

void link_cmd_buzzer_level(uint8_t level) {
    if (level > 2) level = 2;
    char out[4];
    snprintf(out, sizeof(out), "V%u", (unsigned)level);
    link_send(out);
}

void link_cmd_tare(bool set) { link_send(set ? "O1" : "O0"); }

void link_cmd_telemetry_hz(int hz) {
    if (hz < 1)  hz = 1;
    if (hz > 50) hz = 50;
    char out[8];
    snprintf(out, sizeof(out), "F%d", hz);
    link_send(out);
}

// ===========================================================================
//  DIAGNOSTICS
// ===========================================================================
uint32_t link_age_ms() {
    if (g_app.last_rx_ms == 0) return 0xFFFFFFFF;
    return millis() - g_app.last_rx_ms;
}

uint32_t link_baud() { return LINK_BAUD; }

// ===========================================================================
//  THE PUMP  (LVGL task)
// ===========================================================================
static void link_timer_cb(lv_timer_t* /*t*/) {
    pump_rx();

    static uint32_t last_hb = 0;
    if (millis() - last_hb >= HEARTBEAT_MS) {
        last_hb = millis();
        link_send("P");
    }

    // Link dead: forget everything that would otherwise look live. The
    // e-stop and driver flags are cleared too -- the UI shows "No link"
    // instead, rather than a stale e-stop overlay that can never resolve.
    if (g_app.link_ok && link_age_ms() > LINK_TIMEOUT_MS) {
        g_app.link_ok   = false;
        g_app.rig       = RigState::Unknown;
        g_app.regime    = RigRegime::Unknown;
        g_app.estop     = false;
        g_app.drv_fault = false;
    }

    // Re-send preferences whenever the link comes back: the rig may have
    // rebooted onto its compiled-in defaults.
    static bool was_ok = false;
    if (g_app.link_ok && !was_ok) {
        link_cmd_loss_action(g_settings.auto_retract_on_loss);
        link_cmd_estop_action(g_settings.retract_after_estop);
        link_cmd_buzzer_level(g_settings.buzzer_level);
    }
    was_ok = g_app.link_ok;
}

// ---------------------------------------------------------------------------
//  IMPORTANT -- two IDE settings this depends on:
//    Tools > USB CDC On Boot  = Enabled
//    Tools > Core Debug Level = None
// ---------------------------------------------------------------------------
void link_begin() {
    Serial1.setRxBufferSize(512);   // must be called BEFORE begin()
    Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
    lv_timer_create(link_timer_cb, 20, nullptr);
}
