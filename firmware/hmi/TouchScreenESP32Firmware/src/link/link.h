#pragma once
#include <stdint.h>

// ===========================================================================
//  LINK  --  Serial1 connection to the actuator (rig) ESP32.
//
//  PROTOCOL (matches Force_Control_Rig_V4.ino, firmware rev 6.2)
//
//  HMI -> RIG   single-line ASCII commands:
//                   H<kg>   hold/seek a target force, e.g. "H20"
//                   S       stop, return to idle
//                   Z       clear a latched fault (then it auto-retracts,
//                           except after an e-stop with E0)
//                   R       read the load cell once
//                   B       back off ~5 mm (relative home)
//                   +  -    jog forward / backward ~1 mm
//                   Q       beep the rig's buzzer
//                   W<0|1>  on HMI loss: 0 = hold, 1 = retract
//                   E<0|1>  on reset after e-stop: 0 = clear only, 1 = retract
//                   F<hz>   telemetry rate 1..50
//                   V<0|1|2> buzzer volume: off / low / high   (rev 6.1)
//                   O1 / O0  tare now / clear tare               (rev 6.2)
//                   P       heartbeat (silent no-op)
//
//  RIG -> HMI   one telemetry line, 4 Hz idle / 50 Hz during a dynamic run:
//                   T F=12.34 tgt=20.00 pwm=-40 reg=CONT st=RUN lim=0
//                     I=2.41 Ipk=4.6 Td=41.8 es=0 df=0 eot=0
//               plus, when faulted, a trailing free-text field:
//                   ... st=FAULT ... flt=STALL (force not responding)
//
//               I    motor current in A (+ push, - retract)
//               Ipk  peak |current| since the rig booted
//               Td   driver temperature in C (-999 = thermistor fault)
//               es   1 = e-stop pressed (switch open)
//               df   1 = G2 driver fault pin active
//               eot  end of travel: bit0 = extend end, bit1 = retract end
//               N    raw ADS1220 counts, 20 ms average        (rev 6.1)
//               tare kg the rig subtracts from F (0 = none)   (rev 6.2)
//
//               'flt' is ALWAYS the last field because its value contains
//               spaces. The parser handles that specially. Unknown fields are
//               ignored, so an older rig (no I/Td/es...) still works.
// ===========================================================================

// Open Serial1 and start the RX pump. Call ONCE from setup(), inside the
// LVGL lock -- it creates an lv_timer and the LVGL task is already running.
void link_begin();

// ---------------------------------------------------------------------------
//  RAW SEND  (a trailing '\n' is added for you)
//  Prefer the typed helpers below; use this only for one-off experiments.
// ---------------------------------------------------------------------------
void link_send(const char* line);

// ---------------------------------------------------------------------------
//  TYPED COMMANDS
//  These are the ONLY thing screens should call. Keeping the command strings
//  in one place means a protocol change touches exactly one file.
// ---------------------------------------------------------------------------

// "H<kg>" -- start a force-hold run, or change the setpoint of one.
// Returns false (and shows a toast) when a NEW run is refused here because
// the e-stop is pressed or the motor driver is too hot. A setpoint change
// while the rig is already running is always sent.
bool link_cmd_hold(float kg);
void link_cmd_stop();           // "S"      -- stop, back to idle

// "S" then "B": stop AND retract ~5 mm to take the load off the specimen.
// This is what every STOP button uses. Leaving a specimen loaded after a run
// ends is the thing you almost never want, and it is easy to walk away from.
void link_cmd_stop_and_retract();
void link_cmd_reset();          // "Z"      -- clear fault + auto-retract
void link_cmd_read();           // "R"      -- one-shot load-cell reading
void link_cmd_backoff();        // "B"      -- back off ~5 mm
void link_cmd_jog(int dir);     // "+" / "-" -- jog ~1 mm (dir > 0 = forward)

// ---- rev 4 additions -------------------------------------------------
void link_cmd_beep();           // "Q"  -- sound the RIG's buzzer

// "W0"/"W1" -- what the rig should do if THIS display goes silent mid-run.
// Re-sent automatically on every reconnect (see link.cpp).
void link_cmd_loss_action(bool auto_retract);

// "F<hz>" -- telemetry rate, 1..50.
void link_cmd_telemetry_hz(int hz);

// ---- rev 5 additions -------------------------------------------------
// "E0"/"E1" -- whether resetting after an e-stop also retracts ~5 mm.
// Re-sent automatically on every reconnect, like the loss action.
void link_cmd_estop_action(bool retract);

// ---- rev 6.1 additions -----------------------------------------------
// "V0"/"V1"/"V2" -- rig buzzer volume (off / low / high). Re-sent on every
// reconnect. An older rig rejects it harmlessly ("Unknown").
void link_cmd_buzzer_level(uint8_t level);

// ---- rev 6.2 additions -----------------------------------------------
// "O1" = tare (zero) the rig's force reading now, "O0" = clear the tare.
// The rig refuses unless IDLE and the reading is within +/- 2 kg, and keeps
// the tare in flash. Its value comes back in telemetry as tare=.
void link_cmd_tare(bool set);

// ---------------------------------------------------------------------------
//  DIAGNOSTICS
// ---------------------------------------------------------------------------
uint32_t link_age_ms();
uint32_t link_baud();
