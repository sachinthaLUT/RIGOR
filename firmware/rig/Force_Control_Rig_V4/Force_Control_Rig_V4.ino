/* =====================================================================
 *  LOAD-CELL CALIBRATION / LOADING RIG  —  FORCE CONTROL FIRMWARE
 *  Revision 6.2 — Force Rig PCB rev 2 (KiCad, 2026-09-06)
 *
 *  WHAT REV 6.2 CHANGES (2026-10-21, touchscreen v1.3.1). No control changes.
 *    - TARE. The calibration constants (k, c) stay exactly as published; a
 *      separate zero offset is subtracted on top of them, so the reading can
 *      be zeroed in the rig fixture (the cell read 0.11 kg unloaded here,
 *      because the rig loads it differently from the UTM).
 *        O1 = tare now (IDLE only, |reading| <= 2 kg, 0.5 s average)
 *        O0 = clear the tare
 *      Stored in flash (Preferences), so it survives power-off.
 *      Telemetry gains tare=<kg>. N= stays RAW counts (never tared).
 *
 *  WHAT REV 6.1 CHANGES (2026-10-20, touchscreen v1.3.0). No control changes.
 *    - HMI telemetry gains N=<counts>: the raw ADS1220 counts (mean of the
 *      conversions in one 20 ms tick) behind every force value. The
 *      touchscreen's Calibration screen shows and averages them.
 *    - New command V<0|1|2>: buzzer volume off / low / high. The buzzer is
 *      now driven by LEDC PWM; Low is a reduced duty at 20 kHz (inaudible
 *      carrier), tune BUZZER_LOW_DUTY_PCT to taste. Off also silences the
 *      rig's own alarm beeps. Default High; the HMI re-sends on reconnect.
 *
 *  WHAT REV 6 CHANGES (2026-10-16, after Test 11 on the assembled rig):
 *    - PI force control again (Kp 6, Ki 10, output limit 100). The P law
 *      with a fixed pulse level could not push past ~28 kg at 20 kHz.
 *    - FINE ZONE (last 2 kg pushing, 5 kg retracting): the rev 4 pulser is back, at the leadscrew's
 *      break-free level (28 + 0.8 x force PWM, raised automatically if a
 *      pulse does not move it). Continuous drive there stuck, then jumped
 *      0.5-0.8 kg past the target (harness Test 9, 2026-10-19).
 *    - Coarse PI output bounded: at least the break-free level, at most
 *      break-free + 4 PWM per kg of distance to the fine zone, so the
 *      approach slows before the pulses take over.
 *    - ADS1220 at 350 SPS, averaged per 20 ms control tick (was 40 SPS).
 *      With the G2 the force rises at 100-300 kg/s; the old 40 SPS reading
 *      lagged so much that ANY gain overshot by kilograms. A 20 ms average
 *      is one 50 Hz period, so mains pickup is still rejected.
 *    - Hold band: the motor stops once the force is within 0.05 kg below
 *      the target (retracts only if > 0.25 kg above) and the integral
 *      freezes; the self-locking leadscrew holds the force. No hunting.
 *    - 11-bit PWM (values still on the 0-255 scale), CURRENT_SIGN -1,
 *      UTM calibration constants (2026-10-15) in place.
 *
 *  WHAT REV 5 CHANGED:
 *    HARDWARE
 *    - Motor driver is now the Pololu G2 High-Power Motor Driver 18v25
 *      (was Cytron MD20). Same PWM + DIR interface, PWM is now 20 kHz.
 *    - Buzzer moved A3 -> D4. A3 is now the driver thermistor.
 *    - New inputs: ACS773 motor current (A0), e-stop sense (A1),
 *      G2 fault pin (A2), driver NTC thermistor (A3).
 *    - New outputs: bi-colour status LED, red D2 / green D3.
 *    - ADS1220 now RATIOMETRIC: REFP0/REFN0 are wired to the bridge
 *      excitation, so the reference is the excitation itself.
 *
 *    SAFETY
 *    - E-STOP: latched fault the moment the switch opens, in any mode.
 *      Cannot be cleared while the switch is still pressed. Optional
 *      auto-retract on reset, chosen by the HMI ('E' command).
 *    - DRIVER FAULT: G2 FLT pin (short / undervoltage / over-temp). The G2
 *      does not latch its own faults, so we do.
 *    - DRIVER OVER-TEMP: the G2 has NO thermal shutdown of its own.
 *      Warning level blocks new runs; trip level latches a fault.
 *    - OVERCURRENT: sustained current above a trip level.
 *    - CURRENT CLAMP: above a soft limit the applied PWM is scaled back.
 *    - END OF TRAVEL: the actuator's own internal limit switches cut its
 *      power at the ends, which shows up as "driving but ~0 A". That
 *      direction is then blocked; the other is still allowed. If NEITHER
 *      direction draws current it is a "NO MOTOR CURRENT" fault (fuse,
 *      motor unplugged, no motor power).
 *
 *    HMI TELEMETRY gains:  I= Ipk= Td= es= df= eot=   (flt stays LAST)
 *
 *  Revision 4 — 'Q' buzzer, 'W' HMI-loss action + watchdog, 'F' rate.
 *  Revision 3 — Serial1 HMI link, command source tagging.
 *  Revision 2 — control fixes (see "WHY THE PREVIOUS VERSION NEVER SETTLED").
 * =====================================================================
 *
 *  Hardware (PCB rev 2):
 *    - Arduino Nano ESP32 (main controller), powered from 12V_fused on VIN
 *    - ADS1220 24-bit ADC breakout (Olimex BB-ADS1220), SPI, ratiometric
 *    - REF195 5.000 V reference -> ADS1220 AVDD and bridge excitation
 *    - Load cell (4-wire full bridge) on the actuator's pushing end
 *    - Linear actuator (2-wire brushed DC, rated 5 A, internal end switches)
 *    - Pololu G2 18v25 H-bridge (PWM + DIR, FLT, SLP left on its pull-up)
 *    - ACS773LCB-050B Hall current sensor in series with the motor (3.3 V)
 *    - 10k NTC on the driver (10k pull-up to 3V3, NTC to GND)
 *    - 4-terminal e-stop: one contact switches 12V -> MOTOR_VIN, the other
 *      grounds EMER_SENSE. Pressed = both contacts OPEN.
 *    - Active buzzer on D4, bi-colour LED on D2/D3
 *    - Touchscreen (HMI) on Serial1 (D0/D1)
 *    - Limit-switch inputs J8/J9 exist on the PCB but are NOT fitted.
 *
 *  ---------------------------------------------------------------
 *  WHY THE PREVIOUS VERSION NEVER SETTLED  (read this before tuning)
 *  ---------------------------------------------------------------
 *  PWM commands actuator VELOCITY, not force. Against a stiff contact:
 *
 *      force ~= K_stiff * position,   position = integral(velocity dt)
 *
 *  so the PLANT ALREADY CONTAINS AN INTEGRATOR. Five consequences drive
 *  the design below:
 *
 *   (1) REV 6: PI. The push needed to keep moving rises with force
 *       (Test 11: PWM ~ 18 + 0.78 x F), so P alone stalls short of the
 *       target. The integral finds that push by itself. It cannot
 *       limit-cycle because it freezes inside the hold band, where the
 *       motor is off and the leadscrew holds.
 *   (2) REV 6: no pulser and no floor (the integral replaces both).
 *   (3) ASYMMETRIC slew limiting: only increases are rate-limited.
 *   (4) Filtered force + 50/60 Hz rejection.
 *   (5) Contact-regime HYSTERESIS.
 *
 *  CONTROL STRATEGY:
 *      1) APPROACH regime : below the contact threshold, creep at a slow
 *                           fixed speed. (No useful force signal in air.)
 *      2) CONTACT regime  : P force controller with deadband, pulsed
 *                           low-end drive, slew-limited engagement.
 *
 *  SAFETY (latched faults require an explicit reset):
 *    - Overload, stall, approach timeout (rev 2)
 *    - E-stop, driver fault, driver over-temp, overcurrent, end of
 *      travel during a run, no motor current, sensor faults (rev 5)
 *    Software guards are BACKUP to the physical e-stop. Operate attended.
 *
 *  DIRECTION CONVENTION:
 *    "PUSH"    = actuator extends, INCREASES force on the load cell.
 *    "RETRACT" = actuator withdraws, DECREASES / releases force.
 *    G2 truth table: DIR HIGH -> current OUTA -> OUTB. OUTA feeds the ACS773
 *    IP+, so with MOTOR_DIR_PUSH = HIGH a push reads as POSITIVE current.
 *    If the actuator moves the wrong way, swap MOTOR_DIR_PUSH/RETRACT AND
 *    flip CURRENT_SIGN, then check that 'R' shows + current while jogging +.
 *
 *  ---------------------------------------------------------------
 *  BRING-UP ORDER FOR REV 5 — one thing at a time
 *    1. Flash with no load on the cell. Read the self-test on USB: every
 *       line must be [ OK ]. Press and release the e-stop and watch for
 *       "E-STOP PRESSED" / "released".
 *    2. 'R' with nothing on the cell, then with a KNOWN mass. The load-cell
 *       calibration below is PROVISIONAL (see CALIBRATION CONSTANTS):
 *       RECALIBRATE before any test above ~20 kg, because the overload trip
 *       depends on it.
 *    3. Jog + and - with 'R' in between: current sign must follow direction.
 *    4. Small target, e.g. H10. The PWM is now 20 kHz (was ~1 kHz), so
 *       re-check PWM_MIN_MOVE: lower it until the actuator just fails to
 *       move, then add ~5.
 *    5. Only then tune Kp (raise until slight oscillation, back off ~30%).
 * ===================================================================== */

#include <SPI.h>
#include <math.h>
#include <esp_arduino_version.h>
#include <Preferences.h>

// =====================================================================
//  PIN CONFIGURATION   (checked against Force_Rig_PCB.kicad_sch rev 2)
//
//  Nano ESP32 labels (D0..D13, A0..A7) are used on purpose: they are correct
//  whatever Tools > Pin Numbering is set to. Raw GPIO numbers are not.
// =====================================================================
// ---- ADS1220 (SPI, 33R series resistors on SCK/MOSI/MISO) ----
#define ADS1220_CS_PIN     D10   // net CS_ADS1220 (1k pull-up to 3V3)
#define ADS1220_DRDY_PIN   D9    // net DRDY
// SCK / MOSI / MISO = D13 / D11 / D12, the board's default hardware SPI.

// ---- Pololu G2 18v25 ----
#define MOTOR_PWM_PIN      D6    // net PWM
#define MOTOR_DIR_PIN      D5    // net DIR
#define MOTOR_FAULT_PIN    A2    // net MOTOR_FAULT  (open-drain, LOW = fault)
// SLP, CS, VREF are not connected: SLP is pulled high on the G2 (enabled),
// CS is replaced by the ACS773, VREF left at the default 60 A chop limit.
#define MOTOR_DIR_PUSH     HIGH
#define MOTOR_DIR_RETRACT  LOW

// ---- Analog sensors ----
#define CURRENT_SENSE_PIN  A0    // net I_SENSE   (ACS773 VIOUT, 1k/100n RC)
#define THERMISTOR_PIN     A3    // net THERMISTOR (10k to 3V3, NTC to GND)

// ---- Emergency stop ----
#define EMER_SENSE_PIN     A1    // net EMER_SENSE: LOW = released, HIGH = pressed
                                 // (no external pull-up: internal pull-up used)

// ---- Limit switches (J8/J9 on the PCB, NOT FITTED at present) ----
// Kept so they work the moment switches are wired. With nothing connected
// the internal pull-ups read "not hit", so they are harmless.
// NOTE the silkscreen: LS1/D7 goes to J9 "Switch 2", LS2/D8 to J8 "Switch 1".
#define LIMIT_PUSH_PIN     D7    // net LS1
#define LIMIT_RETRACT_PIN  D8    // net LS2

// ---- Buzzer and status LED ----
#define BUZZER_PIN         D4    // net Buzzer+   (WAS A3 on the old board)
#define LED_RED_PIN        D2    // net LEDR (220R)
#define LED_GREEN_PIN      D3    // net LEDG (220R)

// ---- Touchscreen (HMI) link over Serial1 ----
// Wiring to the display board (cross TX/RX, and share a ground):
//    RIG D1 (TX) ---> HMI RX     RIG D0 (RX) <--- HMI TX     GND <--> GND
#define HMI_BAUD        115200
#define HMI_RX_PIN        D0
#define HMI_TX_PIN        D1

const unsigned long HMI_PRESENT_MS  = 5000;
const unsigned long HMI_WATCHDOG_MS = 5000;

// Set by 'W' from the HMI. false = hold position, true = retract 5 mm.
bool hmiLossAutoRetract = false;

// Set by 'V' from the HMI: buzzer volume 0 = off, 1 = low, 2 = high.
// The buzzer is an ACTIVE buzzer (its own oscillator), so "volume" is the
// average supply it sees: PWM at an inaudible 20 kHz, duty below 100 % for
// Low. If Low is too quiet or does not sound at all, RAISE the duty (an
// active buzzer stops oscillating below some minimum voltage).
uint8_t buzzerLevel = 2;
const uint8_t  BUZZER_LOW_DUTY_PCT  = 15;     // tune: 5..60
const uint8_t  BUZZER_PWM_CHANNEL   = 2;      // own LEDC timer (motor uses ch 0)

// Set by 'E' from the HMI. Whether 'Z' after an e-stop also retracts 5 mm.
// Defaults to TRUE to match the HMI's default; the HMI re-sends its setting
// on every reconnect, so a rebooted rig cannot silently disagree for long.
bool estopAutoRetract = true;

// =====================================================================
//  PWM  (G2 supports up to 100 kHz; 20 kHz is inaudible and Pololu's
//  recommendation for sign-magnitude drive)
// =====================================================================
const uint32_t MOTOR_PWM_FREQ_HZ = 20000;
const uint8_t  MOTOR_PWM_BITS    = 10;    // 10-bit steps; values stay on the 0-255 scale (11-bit at 20 kHz can fail)
const uint8_t  MOTOR_PWM_CHANNEL = 0;     // only used on Arduino-ESP32 core 2.x

// =====================================================================
//  LOAD CELL: ADS1220 REFERENCE AND CALIBRATION
// =====================================================================
// 1 = ratiometric (REFP0/REFN0 = bridge excitation, as wired on PCB rev 2)
// 0 = internal 2.048 V reference (old behaviour; use only to compare)
#define ADS_RATIOMETRIC 1

// kg = SLOPE * raw_counts + INTERCEPT
// The ADC data rate does not change the scale (the ADS1220's digital
// filters have unity gain), so the UTM constants hold at 350 SPS.
// REV 6: UTM calibration with PCB rev 2 (2026-10-15), rounds 1-3, linear
// fit 0-100 kg, from LC_Reference. Same values as the calibration harness.
const double CAL_SLOPE_INTERNAL_REF = 0.00002280376925248180;   // rev 4 value (internal ref only)
const double CAL_SLOPE_RATIOMETRIC  = 0.000058327734;
const double CAL_INTERCEPT_KG       = 0.366767338;   // UTM fit c = 0.145367338 + 0.2214 kg plate that loaded the cell during calibration only (LC_Reference!B28)
#define CAL_IS_PROVISIONAL 0

#if ADS_RATIOMETRIC
const double CAL_SLOPE_KG_PER_COUNT = CAL_SLOPE_RATIOMETRIC;
#else
const double CAL_SLOPE_KG_PER_COUNT = CAL_SLOPE_INTERNAL_REF;
#endif

// =====================================================================
//  FORCE / SAFETY LIMITS
// =====================================================================
const double CELL_RATING_KG    = 100.0;
const double OVERLOAD_TRIP_KG  = 95.0;
const double MAX_SETPOINT_KG   = 90.0;

const double CONTACT_ENTER_KG  = 1.5;
const double CONTACT_EXIT_KG   = 0.7;

const double         STALL_DELTA_KG      = 0.3;
const unsigned long  STALL_TIMEOUT_MS    = 4000;
const unsigned long  APPROACH_TIMEOUT_MS = 30000;

// =====================================================================
//  MOTOR CURRENT  (ACS773LCB-050B at 3.3 V: +/-50 A, ~26.4 mV/A, zero at VCC/2)
// =====================================================================
// Verify ACS_SENS_MV_PER_A against your datasheet. The zero point is NOT a
// constant: it is measured at boot with the motor off and then tracked
// slowly whenever the motor is idle.
const float ACS_SENS_MV_PER_A   = 26.4f;
const int   CURRENT_SIGN        = -1;     // measured (Test 11): push now reads positive
const float ACS_ZERO_NOMINAL_MV = 1650.0f;
const float ACS_ZERO_TOL_MV     = 200.0f; // zero further than this from VCC/2 = sensor fault
const float CURRENT_FILTER_ALPHA = 0.30f; // light IIR on the averaged reading

// The actuator is rated 5 A. No stall figure is published, so these are
// PROVISIONAL: watch "Ipk" on the HMI over a few real runs and tighten.
const float I_CLAMP_A      = 6.0f;   // above this the PWM is progressively reduced
const float OC_TRIP_A      = 8.0f;   // above this for OC_TRIP_MS -> latched fault
const unsigned long OC_TRIP_MS = 500;
#define I_CLAMP_ENABLE 1             // 0 = monitor only, never reduce PWM

// End of travel / no motor current
const float EOT_CURRENT_A  = 0.15f;  // below this while driving = no current flowing
const int   EOT_MIN_PWM    = 40;     // only judge when actually driving this hard
const unsigned long EOT_MS = 400;    // ... continuously for this long

// =====================================================================
//  DRIVER TEMPERATURE  (10k NTC, B = 3950, 10k pull-up to 3V3)
// =====================================================================
const float NTC_R0_OHMS     = 10000.0f;
const float NTC_BETA        = 3950.0f;
const float NTC_T0_K        = 298.15f;
const float NTC_RFIX_OHMS   = 10000.0f;   // R11
const float NTC_SUPPLY_MV   = 3300.0f;
const float NTC_OPEN_MV     = 3050.0f;    // ADC tops out ~3.1 V: above this = open circuit
const float NTC_SHORT_MV    = 40.0f;      // below this = shorted
const float TEMP_FILTER_ALPHA = 0.05f;

const float TEMP_WARN_C     = 70.0f;      // new runs refused at or above this
const float TEMP_TRIP_C     = 85.0f;      // latched fault at or above this
const float TEMP_RESET_C    = 70.0f;      // over-temp fault can be cleared below this

// =====================================================================
//  E-STOP / DRIVER FAULT TIMING
// =====================================================================
const unsigned long ESTOP_DEBOUNCE_MS       = 20;
const unsigned long FLT_DEBOUNCE_MS         = 5;
const unsigned long FLT_MASK_AFTER_ESTOP_MS = 1000;  // G2 reports undervoltage while
const unsigned long FLT_MASK_BOOT_MS        = 1000;  // motor power comes and goes

// =====================================================================
//  CONTROL TUNING
// =====================================================================
// PI, identical to Force_Rig_Calibration_Harness_V1. Gains chosen on a
// model fitted to Test 11; verify with harness Test 9 (step response).
double Kp = 6.0;                        // PWM per kg of error   (as used for harness Tests 2-11, 2026-10-19)
double Ki = 10.0;                       // PWM per kg*s
const double PI_PWM_MAX        = 100;   // controller output limit (speed at high force)
const double I_CLAMP           = 150.0; // integral clamp, kg*s
const double HOLD_BELOW_KG     = 0.05;  // stop once within 0.05 kg BELOW target...
const double HOLD_ABOVE_KG     = 0.25;  // ...retract only if > 0.25 kg ABOVE it

const int    PWM_MAX           = 200;   // absolute drive limit (retract/jog use 150-180)
const int    PWM_MIN_MOVE      = 40;
const int    PWM_APPROACH      = 45;
const double DEADBAND_KG       = 0.4;   // "settled" band for reporting (not the hold band)
const double PWM_SLEW_PER_TICK = 6;
// FINE ZONE: last FINE_ZONE_KG before the target uses short pulses at the
// leadscrew's break-free level (identical to the calibration harness).
const double FINE_ZONE_KG       = 2.0;   // push: pulses within this of the target (Test 9: 1 kg was crossed in < 1 tick)
const double RETRACT_FINE_ZONE_KG = 5.0; // retract: pulses within this (continuous retract runs away: the load helps it)
const double COARSE_KAPPA       = 4.0;   // coarse drive <= break-free level + KAPPA x distance to the fine zone (PWM/kg)
const int    PULSE_WINDOW_TICKS = 10;    // 200 ms
// Push level = Test 11 "keeps moving" line + static (break-free) margin.
// The margin shrinks with force because the load path stiffens (Test 11:
// the same PWM moves the force ~5x faster at 50 kg than at 7 kg; Test 9:
// +10 PWM was right at 20 kg but jumped 1-3 kg per pulse at 80 kg).
const double RUN_PWM_AT_0       = 18.3;  // Test 11
const double RUN_PWM_PER_KG     = 0.775; // Test 11
const double STATIC_MARGIN_PWM  = 10.0;  // right at 20 kg (Test 9); x exp(-(F-30)/STIFF_SCALE_KG) above 30 kg
const double STATIC_MARGIN_KNEE_KG = 30.0;
const double STIFF_SCALE_KG     = 35.0;
const double RETRACT_PULSE_PWM  = 40.0;
const double PULSE_PROGRESS_KG  = 0.03;
const double PULSE_ADAPT_STEP   = 3.0;
const double PULSE_ADAPT_MAX    = 30.0;
const int    CONTROL_PERIOD_MS = 20;
const double FORCE_FILTER_ALPHA = 0.5;  // on 20 ms averages of 350 SPS conversions

// =====================================================================
//  RELATIVE-HOME RETRACT / JOG  (open-loop, timed)
// =====================================================================
const double        RETRACT_DIST_MM      = 5.0;
const double        RETRACT_SPEED_MM_S   = 5.0;   // CALIBRATE
const int           RETRACT_PWM          = 180;
const double        RETRACT_FORCE_RISE_ABORT_KG = 2.0;

const double        JOG_DIST_MM        = 1.0;
const double        JOG_SPEED_MM_S     = 5.0;    // CALIBRATE
const int           JOG_PWM            = 150;

// =====================================================================
//  ADS1220 SAMPLING
// =====================================================================
const int SAMPLE_TIMEOUT_MS = 50;

#define ADS1220_CMD_RESET  0x06
#define ADS1220_CMD_START  0x08
#define ADS1220_CMD_RDATA  0x10
#define ADS1220_CMD_RREG   0x20
#define ADS1220_CMD_WREG   0x40

#define ADS1220_REG0_VAL   0x0E   // AINp=AIN0 (SIG+), AINn=AIN1 (SIG-), gain=128, PGA on
#define ADS1220_REG1_VAL   0x74   // REV 6: 350 SPS (turbo, DR 011), continuous (was 0x14 = 40 SPS)
#if ADS_RATIOMETRIC
#define ADS1220_REG2_VAL   0x40   // VREF = REFP0/REFN0; 50/60 Hz FIR off (valid only at 20 SPS) --
#else                             // the 20 ms per-tick average rejects 50 Hz instead
#define ADS1220_REG2_VAL   0x00   // VREF = internal 2.048 V
#endif
#define ADS1220_REG3_VAL   0x00

SPISettings adsSPI(1000000, MSBFIRST, SPI_MODE1);

// =====================================================================
//  CONTROLLER STATE
// =====================================================================
enum Mode { MODE_IDLE, MODE_RUN, MODE_FAULT };
volatile Mode mode = MODE_IDLE;

enum Regime { REG_APPROACH, REG_CONTACT };
Regime regime = REG_APPROACH;

// Why we are faulted. The text goes to the HMI; the code drives reset rules.
enum FaultCode {
  FC_NONE, FC_OVERLOAD, FC_STALL, FC_APPROACH, FC_ESTOP, FC_DRIVER,
  FC_OVERTEMP, FC_OVERCURRENT, FC_EOT, FC_NO_CURRENT, FC_SENSOR
};
FaultCode   faultCode   = FC_NONE;
const char* faultReason = "";
bool        estopDuringFault = false;   // e-stop pressed while this fault was latched

bool   motorEnabled   = false;
int    currentPWM     = 0;
int    currentDirSign = +1;
double targetKg       = 0.0;
double integral       = 0.0;
double prevError      = 0.0;
unsigned long lastControlMs = 0;

double commandedSignedPWM = 0;
int    pulseTick = 0, pulseOnTicks = 1; bool pulseHaveRef = false;
double pulseRefF = 0, pulseAdaptPush = 0, pulseAdaptRet = 0;

double forceFiltered = 0.0;
bool   forceFilterSeeded = false;

unsigned long stallSince      = 0;
bool          stallTimerRunning = false;
double        stallRefForce   = 0.0;
bool          commandingMotion = false;

volatile bool limitPushHit    = false;
volatile bool limitRetractHit = false;

// ---- Motor current ----
float iZeroMv     = ACS_ZERO_NOMINAL_MV;
float iRawA       = 0.0f;    // this sample (8-read average), unfiltered
float iAmps       = 0.0f;    // filtered
float iPeakA      = 0.0f;    // largest |current| since boot
bool  iSensorOk   = false;
int   iLimitPwm   = 255;     // current-clamp ceiling on PWM magnitude
unsigned long ocSince = 0;
bool  ocTiming    = false;
unsigned long motorOffSince = 0;   // for zero tracking

// ---- End of travel (actuator's internal switches) ----
bool  eotPush     = false;
bool  eotRetract  = false;
int   eotDir      = 0;
unsigned long eotSince = 0;
int   lastAppliedForEot = 0;   // PWM applied BEFORE this current sample

// ---- Driver temperature ----
float tempC       = 25.0f;
bool  tempOk      = false;
bool  tempSeeded  = false;
float tempRawMv   = 0.0f;

// ---- E-stop / driver fault pin ----
bool  estopActive      = false;
bool  estopRawLast     = false;
unsigned long estopRawSince  = 0;
unsigned long estopChangedMs = 0;
bool  driverFaultLive  = false;
bool  fltLowTiming     = false;
unsigned long fltLowSince = 0;

// ---- HMI link bookkeeping ----
unsigned long lastHmiRxMs   = 0;
bool          hmiWasPresent = false;
bool          cmdFromHmi    = false;

bool systemHealthy = false;
unsigned long hmiTelemetryPeriodMs = 250;

// Forward declarations (Arduino auto-prototypes most, but not all).
void stopRun();
void doRelativeRetract();
void jogMove(int dirSign);
void reportForce();
double readForceKgRaw();
double readForceKgFiltered();
bool setTarget(double kg);
void startRun(double kg);
void clearFault();
void printHelp();
void pollHmiSerial();
void emitHmiTelemetry(double, int);
void enterFault(FaultCode code, const char* why);
void pollSafetyInputs();
void beep(int ms);               // defined later (default arg 200 is given there)
void sampleCurrent();
void sampleTemperature();
void eotCheck(int appliedSigned);
void motorStop();

// =====================================================================
//  ADS1220 DRIVER
// =====================================================================
void adsWriteReg(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_WREG | (reg << 2));
  SPI.transfer(val);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
}

uint8_t adsReadReg(uint8_t reg) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RREG | (reg << 2));
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
  return val;
}

void adsCommand(uint8_t cmd) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(cmd);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
}

long adsReadRaw() {
  uint8_t b0, b1, b2;
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RDATA);
  b0 = SPI.transfer(0x00);
  b1 = SPI.transfer(0x00);
  b2 = SPI.transfer(0x00);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();

  long raw = ((long)b0 << 16) | ((long)b1 << 8) | b2;
  if (raw & 0x800000) raw |= 0xFF000000;
  return raw;
}

bool adsInit() {
  adsCommand(ADS1220_CMD_RESET);
  delay(100);
  adsWriteReg(0x00, ADS1220_REG0_VAL);
  adsWriteReg(0x01, ADS1220_REG1_VAL);
  adsWriteReg(0x02, ADS1220_REG2_VAL);
  adsWriteReg(0x03, ADS1220_REG3_VAL);

  bool ok = (adsReadReg(0x00) == ADS1220_REG0_VAL) &&
            (adsReadReg(0x01) == ADS1220_REG1_VAL) &&
            (adsReadReg(0x02) == ADS1220_REG2_VAL) &&
            (adsReadReg(0x03) == ADS1220_REG3_VAL);

  adsCommand(ADS1220_CMD_START);
  delay(100);
  return ok;
}

bool adsDataReady() {
  return digitalRead(ADS1220_DRDY_PIN) == LOW;
}

// REV 6: conversion accumulator. adsPoll() (called on every loop pass)
// collects each 350 SPS conversion; readForceKgRaw() returns the AVERAGE
// since the last call (~7 conversions per 20 ms tick). 20 ms is exactly one
// 50 Hz period, so mains pickup averages out.
double adsAccSum = 0; int adsAccN = 0; bool adsAccSat = false; long adsAccSatVal = 0;
unsigned long adsAccStartMs = 0; long adsLastCounts = 0;

// REV 6.2: tare. Subtracted from every force reading, ON TOP of the published
// calibration (k, c), which therefore never changes. Set by 'O1', cleared by
// 'O0', persisted in flash.
double tareOffsetKg = 0.0;
const double TARE_MAX_KG = 2.0;     // refuse to tare more than this: a load is on the cell
Preferences rigPrefs;
void adsAccReset() { adsAccSum = 0; adsAccN = 0; adsAccSat = false; }
void adsPoll() {
  if (!adsDataReady()) return;
  long c = adsReadRaw();
  if (adsAccN == 0) adsAccStartMs = millis();
  if (c >= 8388607L || c <= -8388608L) { adsAccSat = true; adsAccSatVal = c; }
  adsAccSum += (double)c; adsAccN++;
}
double readForceKgRaw() {
  adsPoll();
  if (adsAccN > 0 && millis() - adsAccStartMs > 40) adsAccReset();   // stale window
  unsigned long t0 = millis();
  while (adsAccN == 0 && millis() - t0 < (unsigned long)SAMPLE_TIMEOUT_MS) adsPoll();
  if (adsAccN > 0) adsLastCounts = adsAccSat ? adsAccSatVal : lround(adsAccSum / adsAccN);
  adsAccReset();
  return CAL_SLOPE_KG_PER_COUNT * (double)adsLastCounts + CAL_INTERCEPT_KG - tareOffsetKg;
}

// =====================================================================
//  TARE  (rev 6.2)
//  Zeroes the reading in the rig fixture without touching k or c. Averages
//  0.5 s of UNTARED readings with the actuator idle; refuses if that average
//  is over TARE_MAX_KG, which means something is loading the cell.
// =====================================================================
void saveTare() {
  if (rigPrefs.begin("rigor", false)) { rigPrefs.putDouble("tare", tareOffsetKg); rigPrefs.end(); }
}
void loadTare() {
  tareOffsetKg = 0.0;
  if (rigPrefs.begin("rigor", true)) { tareOffsetKg = rigPrefs.getDouble("tare", 0.0); rigPrefs.end(); }
  if (!(fabs(tareOffsetKg) <= TARE_MAX_KG)) tareOffsetKg = 0.0;   // also catches NaN
}
void doTare(bool set) {
  if (!set) {
    tareOffsetKg = 0.0;
    saveTare();
    forceFilterSeeded = false;
    Serial.println(F(">> Tare cleared (0.000 kg)."));
    return;
  }
  if (mode != MODE_IDLE) { Serial.println(F("Tare refused: rig must be IDLE (stop / reset first).")); return; }

  double sum = 0; int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 500) {
    sum += readForceKgRaw() + tareOffsetKg;     // untared reading
    n++;
    pollSafetyInputs();
  }
  if (n == 0) { Serial.println(F("Tare failed: no ADC data.")); return; }
  if (mode != MODE_IDLE) { Serial.println(F("Tare aborted: fault during averaging.")); return; }
  const double z = sum / n;
  if (fabs(z) > TARE_MAX_KG) {
    Serial.print(F("Tare refused: reading ")); Serial.print(z, 3);
    Serial.println(F(" kg - is something loading the cell?"));
    return;
  }
  tareOffsetKg = z;
  saveTare();
  forceFilterSeeded = false;                     // no slow glide to the new zero
  Serial.print(F(">> Tare set: ")); Serial.print(tareOffsetKg, 3);
  Serial.print(F(" kg (")); Serial.print(n); Serial.println(F(" readings)."));
  beep(80);
}

double readForceKgFiltered() {
  double raw = readForceKgRaw();
  if (!forceFilterSeeded) {
    forceFiltered = raw;
    forceFilterSeeded = true;
  } else {
    forceFiltered += FORCE_FILTER_ALPHA * (raw - forceFiltered);
  }
  return forceFiltered;
}

// =====================================================================
//  PWM OUTPUT (LEDC, 20 kHz)
//  analogWrite() is not used: its frequency is a core default (~1 kHz)
//  and that is audible and leaves a lot of current ripple.
// =====================================================================
bool pwmOk = false;
void pwmBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  pwmOk = ledcAttach(MOTOR_PWM_PIN, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS);
#else
  pwmOk = ledcSetup(MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS) != 0;
  ledcAttachPin(MOTOR_PWM_PIN, MOTOR_PWM_CHANNEL);
#endif
}

// Duty on the 0-255 scale used everywhere; written at MOTOR_PWM_BITS.
void pwmWriteF(double duty) {
  if (duty < 0)   duty = 0;
  if (duty > 255) duty = 255;
  uint32_t d = (uint32_t)lround(duty * ((1 << MOTOR_PWM_BITS) - 1) / 255.0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MOTOR_PWM_PIN, d);
#else
  ledcWrite(MOTOR_PWM_CHANNEL, d);
#endif
}
void pwmWrite(int duty) { pwmWriteF((double)duty); }

// =====================================================================
//  MOTOR DRIVER (Pololu G2: PWM + DIR, PWM low = brake)
// =====================================================================
void applyMotorF(double signedPWM) {
  // Hard gate: never drive if disabled, faulted or e-stopped.
  if (!motorEnabled || mode == MODE_FAULT || estopActive) {
    motorStop();
    return;
  }

  // Block motion INTO a known end: limit switches (if ever fitted) and the
  // actuator's own end-of-travel detected from current.
  if (signedPWM > 0 && (limitPushHit    || eotPush))    signedPWM = 0;
  if (signedPWM < 0 && (limitRetractHit || eotRetract)) signedPWM = 0;

  double mag = fabs(signedPWM);
  if (mag > PWM_MAX) mag = PWM_MAX;
#if I_CLAMP_ENABLE
  if (mag > iLimitPwm) mag = iLimitPwm;     // soft current clamp
#endif

  if (mag < 0.1) {
    motorStop();
    return;
  }

  digitalWrite(MOTOR_DIR_PIN, (signedPWM > 0) ? MOTOR_DIR_PUSH : MOTOR_DIR_RETRACT);
  pwmWriteF(mag);

  currentPWM     = (int)lround(mag); if (currentPWM < 1) currentPWM = 1;
  currentDirSign = (signedPWM > 0) ? +1 : -1;
}
void applyMotor(int signedPWM) { applyMotorF((double)signedPWM); }

// Applied PWM as a signed number, for telemetry and end-of-travel checks.
int appliedSignedPWM() { return currentPWM * currentDirSign; }

void motorStop() {
  pwmWrite(0);
  if (currentPWM != 0) motorOffSince = millis();
  currentPWM = 0;
  // With motor power cut by the e-stop, the G2 is unpowered. Drive DIR low
  // as well so its inputs are not back-feeding it through protection diodes.
  if (estopActive) digitalWrite(MOTOR_DIR_PIN, LOW);
}

void enableMotor(bool en) {
  motorEnabled = en;
  if (!en) motorStop();
}

// =====================================================================
//  BUZZER / LED HELPERS
// =====================================================================
// Buzzer on LEDC at the motor's PWM frequency and resolution, on its own
// channel. Same frequency on purpose: even if a core version put it on the
// motor's timer, nothing would change for the motor.
bool buzzerPwmOk = false;
void buzzerBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  buzzerPwmOk = ledcAttachChannel(BUZZER_PIN, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS, BUZZER_PWM_CHANNEL);
#else
  buzzerPwmOk = ledcSetup(BUZZER_PWM_CHANNEL, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS) != 0;
  ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CHANNEL);
#endif
  if (!buzzerPwmOk) { pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); }
}
void buzzerWrite(bool on) {
  if (!buzzerPwmOk) { digitalWrite(BUZZER_PIN, on && buzzerLevel ? HIGH : LOW); return; }
  const uint32_t full = (1UL << MOTOR_PWM_BITS) - 1;
  uint32_t d = 0;
  if (on && buzzerLevel == 2) d = full;
  else if (on && buzzerLevel == 1) d = full * BUZZER_LOW_DUTY_PCT / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER_PIN, d);
#else
  ledcWrite(BUZZER_PWM_CHANNEL, d);
#endif
}

void beep(int ms = 200) {
  if (buzzerLevel == 0) return;      // Off: no sound, and no delay either
  buzzerWrite(true);
  delay(ms);
  buzzerWrite(false);
}
void alarmPattern() {
  for (int i = 0; i < 3; i++) { beep(150); delay(100); }
}

// Status LED:
//   green solid = ready        green 1 Hz blink = running
//   red solid   = fault        red 4 Hz blink   = e-stop pressed
//   red 1 Hz    = self-test failed (idle)
void updateLeds() {
  unsigned long t = millis();
  bool r = false, g = false;
  if (estopActive)                 r = (t / 125) % 2;
  else if (mode == MODE_FAULT)     r = true;
  else if (mode == MODE_RUN)       g = (t / 500) % 2;
  else if (!systemHealthy)         r = (t / 500) % 2;
  else                             g = true;
  digitalWrite(LED_RED_PIN,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, g ? HIGH : LOW);
}

// =====================================================================
//  LIMIT-SWITCH INTERRUPTS (not fitted; kept for later)
//  Flags only: the motor is stopped by applyMotor() on the next pass.
//  Calling the LEDC driver from an ISR is not safe on all core versions.
// =====================================================================
void IRAM_ATTR isrLimitPush()    { limitPushHit    = (digitalRead(LIMIT_PUSH_PIN) == LOW); }
void IRAM_ATTR isrLimitRetract() { limitRetractHit = (digitalRead(LIMIT_RETRACT_PIN) == LOW); }

// =====================================================================
//  ANALOG SENSORS
// =====================================================================
float readMvAvg(uint8_t pin, int n) {
  uint32_t sum = 0;
  for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
  return (float)sum / (float)n;
}

// Measure the current-sensor zero with the motor OFF. Called once at boot.
void currentZeroAtBoot() {
  motorStop();
  delay(50);
  iZeroMv  = readMvAvg(CURRENT_SENSE_PIN, 256);
  iSensorOk = fabsf(iZeroMv - ACS_ZERO_NOMINAL_MV) < ACS_ZERO_TOL_MV;
  iAmps = 0.0f; iRawA = 0.0f;
}

void sampleCurrent() {
  float mv = readMvAvg(CURRENT_SENSE_PIN, 8);
  iRawA = CURRENT_SIGN * (mv - iZeroMv) / ACS_SENS_MV_PER_A;
  iAmps += CURRENT_FILTER_ALPHA * (iRawA - iAmps);
  if (fabsf(iAmps) > iPeakA) iPeakA = fabsf(iAmps);

  // Slow zero tracking while the motor has been off for a while. With the
  // G2 braking and the leadscrew self-locking, true current is zero then.
  // Small corrections only, so a real current can never be "tracked away".
  if (currentPWM == 0 && !estopActive && millis() - motorOffSince > 500) {
    if (fabsf(mv - iZeroMv) < 60.0f) iZeroMv += 0.01f * (mv - iZeroMv);
  }

  // Soft clamp: shrink the allowed PWM while current is above the clamp
  // level, recover slowly once it drops. Never below PWM_MIN_MOVE, or the
  // actuator could not move at all.
  if (fabsf(iAmps) > I_CLAMP_A) iLimitPwm -= 4;
  else                          iLimitPwm += 2;
  if (iLimitPwm < PWM_MIN_MOVE) iLimitPwm = PWM_MIN_MOVE;
  if (iLimitPwm > 255)          iLimitPwm = 255;

  // Overcurrent trip (sustained).
  if (fabsf(iAmps) > OC_TRIP_A) {
    if (!ocTiming) { ocTiming = true; ocSince = millis(); }
    else if (millis() - ocSince > OC_TRIP_MS && mode != MODE_FAULT) {
      enterFault(FC_OVERCURRENT, "OVERCURRENT");
    }
  } else {
    ocTiming = false;
  }
}

void sampleTemperature() {
  float mv = readMvAvg(THERMISTOR_PIN, 4);
  tempRawMv = mv;
  if (mv > NTC_OPEN_MV || mv < NTC_SHORT_MV) { tempOk = false; return; }
  float r = NTC_RFIX_OHMS * mv / (NTC_SUPPLY_MV - mv);
  float t = 1.0f / (1.0f / NTC_T0_K + logf(r / NTC_R0_OHMS) / NTC_BETA) - 273.15f;
  if (!tempSeeded) { tempC = t; tempSeeded = true; }
  else             { tempC += TEMP_FILTER_ALPHA * (t - tempC); }
  tempOk = true;
}

// =====================================================================
//  END OF TRAVEL
//  The actuator cuts its own power at each end, so "we are driving but no
//  current flows" means we are at the end in that direction.
//
//  Judged on the RAW current sample, against the PWM that was applied
//  BEFORE the sample was taken, and only while that PWM has been on for two
//  consecutive samples. The pulser's off-phases and the filter lag would
//  otherwise both look like "no current". Any single sample with current
//  resets the timer, so noise can only delay detection, never fake it.
// =====================================================================
void declareEot(int dir) {
  if (dir > 0) eotPush = true; else eotRetract = true;
  motorStop();
  Serial.print(F("!! END OF TRAVEL: "));
  Serial.println(dir > 0 ? F("extend end (no current while pushing)")
                         : F("retract end (no current while retracting)"));

  if (eotPush && eotRetract) {
    enterFault(FC_NO_CURRENT, "NO MOTOR CURRENT (check fuse / motor wiring)");
  } else if (mode == MODE_RUN) {
    enterFault(FC_EOT, dir > 0 ? "END OF TRAVEL (extend) - no contact"
                               : "END OF TRAVEL (retract)");
  }
}

void eotCheck(int appliedNow) {
  static int prevApplied = 0;
  int judged = lastAppliedForEot;          // what was driving during this sample
  bool steady = (judged != 0) && (prevApplied != 0) &&
                ((judged > 0) == (prevApplied > 0)) &&
                abs(judged) >= EOT_MIN_PWM && abs(prevApplied) >= EOT_MIN_PWM;
  prevApplied       = lastAppliedForEot;
  lastAppliedForEot = appliedNow;

  if (!steady || estopActive) { eotDir = 0; return; }

  int dir = (judged > 0) ? +1 : -1;
  if (fabsf(iRawA) < EOT_CURRENT_A) {
    if (eotDir != dir) { eotDir = dir; eotSince = millis(); }
    else if (millis() - eotSince > EOT_MS) { eotDir = 0; declareEot(dir); }
  } else {
    eotDir = 0;
    // Current is flowing, so we really are moving in this direction: the
    // OPPOSITE end can no longer be where we are.
    if (dir > 0) eotRetract = false; else eotPush = false;
  }
}

// =====================================================================
//  E-STOP AND DRIVER FAULT PIN  (polled every pass, and inside blocking moves)
// =====================================================================
void onEstopPressed() {
  motorStop();                      // estopActive is already true: DIR goes low too
  enableMotor(false);
  commandedSignedPWM = 0;
  Serial.println(F("!! E-STOP PRESSED - motor power cut at the switch."));
  if (mode == MODE_FAULT) {
    estopDuringFault = true;        // keep the original reason, remember the e-stop
    alarmPattern();
  } else {
    enterFault(FC_ESTOP, "E-STOP");
    estopDuringFault = true;
  }
}

void pollSafetyInputs() {
  unsigned long now = millis();

  // ---- E-stop: open = pressed. Debounced both ways. ----
  bool raw = (digitalRead(EMER_SENSE_PIN) == HIGH);
  if (raw != estopRawLast) { estopRawLast = raw; estopRawSince = now; }
  if (raw != estopActive && now - estopRawSince >= ESTOP_DEBOUNCE_MS) {
    estopActive    = raw;
    estopChangedMs = now;
    if (estopActive) onEstopPressed();
    else             Serial.println(F(">> E-stop released. Send 'Z' to reset."));
  }

  // ---- Driver FLT (open drain, LOW = fault). ----
  // Masked while the e-stop has motor power off, and briefly after it
  // changes or after boot: the G2 legitimately reports undervoltage then,
  // and that must not overwrite "E-STOP" as the reason shown.
  bool fltLow = (digitalRead(MOTOR_FAULT_PIN) == LOW);
  bool masked = estopActive ||
                (now - estopChangedMs < FLT_MASK_AFTER_ESTOP_MS) ||
                (now < FLT_MASK_BOOT_MS);
  driverFaultLive = fltLow && !masked;
  if (driverFaultLive) {
    if (!fltLowTiming) { fltLowTiming = true; fltLowSince = now; }
    else if (now - fltLowSince >= FLT_DEBOUNCE_MS && mode != MODE_FAULT) {
      motorStop();
      enterFault(FC_DRIVER, "DRIVER FAULT (short / undervoltage / over-temp)");
    }
  } else {
    fltLowTiming = false;
  }
}

// Everything that must keep running while a move blocks. Returns false if
// the move must abort now.
bool serviceDuringMove(double forceKg, int appliedSigned) {
  pollSafetyInputs();
  sampleCurrent();
  eotCheck(appliedSigned);
  updateLeds();
  emitHmiTelemetry(forceKg, appliedSigned);
  pollHmiSerial();
  return !(estopActive || mode == MODE_FAULT);
}

// =====================================================================
//  RELATIVE-HOME RETRACT  (open-loop, timed, guarded)
// =====================================================================
void doRelativeRetract() {
  double seconds = RETRACT_DIST_MM / RETRACT_SPEED_MM_S;
  unsigned long durationMs = (unsigned long)(seconds * 1000.0);

  Serial.print(F(">> Retract ~")); Serial.print(RETRACT_DIST_MM, 1);
  Serial.print(F(" mm (")); Serial.print(seconds, 2);
  Serial.println(F(" s, open-loop)."));

  double startForce = readForceKgFiltered();
  enableMotor(true);

  unsigned long t0 = millis();
  bool aborted = false;
  const char* abortWhy = "";

  while (millis() - t0 < durationMs) {
    if (limitRetractHit || eotRetract) { abortWhy = "retract end reached"; aborted = true; break; }

    double f = readForceKgFiltered();
    if (f - startForce > RETRACT_FORCE_RISE_ABORT_KG) {
      abortWhy = "force rose during retract (wrong dir/jam)";
      aborted = true; break;
    }
    if (fabs(f) > OVERLOAD_TRIP_KG) { abortWhy = "overload during retract"; aborted = true; break; }

    applyMotor(-RETRACT_PWM);
    if (!serviceDuringMove(f, appliedSignedPWM())) { abortWhy = "e-stop / fault"; aborted = true; break; }
    delay(5);
  }

  motorStop();
  enableMotor(false);
  commandedSignedPWM = 0;

  if (aborted) { Serial.print(F("   Retract stopped early: ")); Serial.println(abortWhy); }
  else         { Serial.println(F("   Retract done.")); }
  beep(120);
}

// =====================================================================
//  MANUAL JOG  (open-loop, timed)  — '+' and '-'.
// =====================================================================
void jogMove(int dirSign) {
  if (mode == MODE_FAULT) { Serial.println(F("In FAULT. Send 'Z' first.")); return; }
  if (estopActive)        { Serial.println(F("E-stop pressed. Release it first.")); return; }
  stopRun();

  if (dirSign > 0 && eotPush)    { Serial.println(F("At extend end of travel - jog refused.")); return; }
  if (dirSign < 0 && eotRetract) { Serial.println(F("At retract end of travel - jog refused.")); return; }

  double seconds = JOG_DIST_MM / JOG_SPEED_MM_S;
  unsigned long durationMs = (unsigned long)(seconds * 1000.0);

  Serial.print(F(">> Jog ")); Serial.print(dirSign > 0 ? F("FORWARD +") : F("BACK -"));
  Serial.print(JOG_DIST_MM, 1); Serial.println(F(" mm (open-loop)."));

  enableMotor(true);
  unsigned long t0 = millis();
  bool aborted = false;
  const char* why = "";

  while (millis() - t0 < durationMs) {
    double f = readForceKgFiltered();
    if (fabs(f) > OVERLOAD_TRIP_KG) { why = "overload"; aborted = true; break; }
    if (dirSign > 0 && f > MAX_SETPOINT_KG) { why = "reached max setpoint force"; aborted = true; break; }
    if (dirSign > 0 && (limitPushHit || eotPush))       { why = "extend end reached";  aborted = true; break; }
    if (dirSign < 0 && (limitRetractHit || eotRetract)) { why = "retract end reached"; aborted = true; break; }

    applyMotor(dirSign > 0 ? +JOG_PWM : -JOG_PWM);
    if (!serviceDuringMove(f, appliedSignedPWM())) { why = "e-stop / fault"; aborted = true; break; }
    delay(5);
  }

  motorStop();
  enableMotor(false);
  commandedSignedPWM = 0;

  if (aborted) { Serial.print(F("   Jog stopped early: ")); Serial.println(why); }
  else         { Serial.println(F("   Jog done.")); }
}

// =====================================================================
//  READ ON DEMAND  ('R' command)  -- load cell plus the new sensors
// =====================================================================
void reportForce() {
  const int N = 20;
  double sumKg = 0, minKg = 1e9, maxKg = -1e9;
  long   lastCounts = 0;
  for (int i = 0; i < N; i++) {
    unsigned long t0 = millis();
    while (!adsDataReady()) { if (millis() - t0 > SAMPLE_TIMEOUT_MS) break; }
    lastCounts = adsReadRaw();
    double kg = CAL_SLOPE_KG_PER_COUNT * (double)lastCounts + CAL_INTERCEPT_KG;
    sumKg += kg;
    if (kg < minKg) minKg = kg;
    if (kg > maxKg) maxKg = kg;
    delay(5);
  }
  double avgKg = sumKg / N;
  Serial.print(F(">> Load cell: "));
  Serial.print(avgKg, 3);
  Serial.print(F(" kg   (spread "));
  Serial.print(maxKg - minKg, 3);
  Serial.print(F(" kg, raw counts ~"));
  Serial.print(lastCounts);
  Serial.println(F(")"));
#if CAL_IS_PROVISIONAL
  Serial.println(F("   NOTE: calibration is PROVISIONAL - use raw counts to recalibrate."));
#endif

  Serial.print(F(">> Motor current: ")); Serial.print(iAmps, 2);
  Serial.print(F(" A  (peak ")); Serial.print(iPeakA, 2);
  Serial.print(F(" A, zero ")); Serial.print(iZeroMv, 0); Serial.println(F(" mV)"));
  Serial.print(F(">> Driver temp:   "));
  if (tempOk) { Serial.print(tempC, 1); Serial.println(F(" C")); }
  else        { Serial.print(F("SENSOR FAULT (")); Serial.print(tempRawMv, 0); Serial.println(F(" mV)")); }
  Serial.print(F(">> E-stop: ")); Serial.print(estopActive ? F("PRESSED") : F("released"));
  Serial.print(F("   Driver FLT: ")); Serial.print(driverFaultLive ? F("FAULT") : F("ok"));
  Serial.print(F("   End of travel: "));
  Serial.print(eotPush ? F("extend ") : F(""));
  Serial.print(eotRetract ? F("retract") : F(""));
  if (!eotPush && !eotRetract) Serial.print(F("none"));
  Serial.println();
}

// =====================================================================
//  FAULT HANDLING (latched)
// =====================================================================
void enterFault(FaultCode code, const char* why) {
  mode = MODE_FAULT;
  faultCode = code;
  faultReason = why;
  estopDuringFault = (code == FC_ESTOP);
  motorStop();
  enableMotor(false);
  commandedSignedPWM = 0;
  Serial.print(F("!! FAULT: "));
  Serial.println(why);
  Serial.println(F("   Motor latched off. Check the rig, then send 'Z' to reset."));
  alarmPattern();
}

void clearFault() {
  if (mode != MODE_FAULT) { Serial.println(F("Not in fault.")); return; }

  // Conditions that must be gone before the fault can be cleared.
  if (estopActive) { Serial.println(F("Release the E-STOP first.")); return; }
  if (digitalRead(MOTOR_FAULT_PIN) == LOW && millis() - estopChangedMs > FLT_MASK_AFTER_ESTOP_MS) {
    Serial.println(F("Driver still reports a fault (FLT low) - not clearing."));
    return;
  }
  if (faultCode == FC_OVERTEMP && (!tempOk || tempC >= TEMP_RESET_C)) {
    Serial.print(F("Driver still too hot - wait until below "));
    Serial.print(TEMP_RESET_C, 0); Serial.println(F(" C."));
    return;
  }
  double f = readForceKgFiltered();
  if (fabs(f) > OVERLOAD_TRIP_KG) {
    Serial.println(F("Still overloaded — relieve the load before reset."));
    return;
  }

  bool retract = !(estopDuringFault && !estopAutoRetract);

  // "No current either way" may have been a blown fuse now replaced: give
  // both directions another chance.
  if (eotPush && eotRetract) { eotPush = false; eotRetract = false; }

  mode = MODE_IDLE;
  faultCode = FC_NONE;
  estopDuringFault = false;
  integral = 0; prevError = 0; currentPWM = 0;
  commandedSignedPWM = 0;
  stallTimerRunning = false;
  ocTiming = false;
  Serial.println(F(">> Fault cleared."));

  if (retract) doRelativeRetract();
  else         Serial.println(F("   (no retract: e-stop reset with auto-retract OFF)"));

  Serial.println(F(">> Idle."));
}

// =====================================================================
//  SAFETY CHECKS (every control tick)
// =====================================================================
bool overloadCheck(double forceKg) {
  if (fabs(forceKg) > OVERLOAD_TRIP_KG) {
    if (mode != MODE_FAULT) enterFault(FC_OVERLOAD, "OVERLOAD");
    return true;
  }
  return false;
}

void temperatureCheck() {
  if (tempOk && tempC >= TEMP_TRIP_C && mode != MODE_FAULT) {
    enterFault(FC_OVERTEMP, "DRIVER OVER-TEMP");
  } else if (!tempOk && mode == MODE_RUN) {
    enterFault(FC_SENSOR, "SENSOR FAULT (driver thermistor)");
  }
}

void stallCheck(double forceKg) {
  if (!commandingMotion) { stallTimerRunning = false; return; }
  // Within 1 kg of the target the PI may legitimately creep slowly.
  if (regime == REG_CONTACT && fabs(targetKg - forceKg) <= 1.0) { stallTimerRunning = false; return; }

  unsigned long limitMs = (regime == REG_APPROACH) ? APPROACH_TIMEOUT_MS
                                                   : STALL_TIMEOUT_MS;
  if (!stallTimerRunning) {
    stallTimerRunning = true;
    stallSince    = millis();
    stallRefForce = forceKg;
    return;
  }
  if (fabs(forceKg - stallRefForce) > STALL_DELTA_KG) {
    stallTimerRunning = false;
  } else if (millis() - stallSince > limitMs) {
    if (regime == REG_APPROACH) enterFault(FC_APPROACH, "APPROACH TIMEOUT (never made contact)");
    else                        enterFault(FC_STALL,    "STALL (force not responding)");
  }
}

// =====================================================================
//  CORE CONTROL  (unchanged from rev 2)
// =====================================================================
double computeControl(double forceKg, double dtSec) {
  if (targetKg < CONTACT_ENTER_KG) {
    regime = REG_CONTACT;
  } else if (regime == REG_APPROACH) {
    if (forceKg > CONTACT_ENTER_KG) regime = REG_CONTACT;
  } else {
    if (forceKg < CONTACT_EXIT_KG)  regime = REG_APPROACH;
  }

  if (regime == REG_APPROACH) {
    integral  = 0;
    prevError = targetKg - forceKg;
    return +PWM_APPROACH;
  }

  double error = targetKg - forceKg;
  prevError = error;

  // Hold band: motor off, integral frozen, the leadscrew holds the force.
  if (error <= HOLD_BELOW_KG && error >= -HOLD_ABOVE_KG) return 0;

  if (error * integral < 0) integral = 0;          // crossed the target: drop old windup
  double out = Kp * error + Ki * integral;
  bool intoLimit = fabs(out) >= PI_PWM_MAX && ((out > 0) == (error > 0));
  if (!intoLimit && (error > FINE_ZONE_KG || error < -RETRACT_FINE_ZONE_KG)) {  // anti-windup; frozen in the fine zones
    integral += error * dtSec;
    if (integral >  I_CLAMP) integral =  I_CLAMP;
    if (integral < -I_CLAMP) integral = -I_CLAMP;
  }
  if (out >  PI_PWM_MAX) out =  PI_PWM_MAX;
  if (out < -PI_PWM_MAX) out = -PI_PWM_MAX;
  return out;
}

double slewLimit(double desiredSignedPWM) {
  bool sameDir = ((desiredSignedPWM >= 0) == (commandedSignedPWM >= 0));
  bool rampingUp = sameDir && (fabs(desiredSignedPWM) > fabs(commandedSignedPWM));

  if (rampingUp) {
    if (desiredSignedPWM > commandedSignedPWM) {
      commandedSignedPWM += PWM_SLEW_PER_TICK;
      if (commandedSignedPWM > desiredSignedPWM) commandedSignedPWM = desiredSignedPWM;
    } else {
      commandedSignedPWM -= PWM_SLEW_PER_TICK;
      if (commandedSignedPWM < desiredSignedPWM) commandedSignedPWM = desiredSignedPWM;
    }
  } else {
    commandedSignedPWM = desiredSignedPWM;
  }
  return commandedSignedPWM;
}

double stiffScale(double f) { return fmin(1.0, exp(-(fmax(f, 0.0) - 10.0) / STIFF_SCALE_KG)); }
double pushLevel(double f)  { return RUN_PWM_AT_0 + RUN_PWM_PER_KG * fmax(f, 0.0)
                                     + STATIC_MARGIN_PWM * fmin(1.0, exp(-(fmax(f, 0.0) - STATIC_MARGIN_KNEE_KG) / STIFF_SCALE_KG))
                                     + pulseAdaptPush; }
// REV 6: PI (slew-limited) outside the fine zone, bounded pulses inside it.
double controlStep(double forceKg, double dtSec) {
  double u = computeControl(forceKg, dtSec);
  if (regime == REG_APPROACH) { pulseTick = 0; pulseHaveRef = false; return slewLimit(u); }
  if (u == 0) { pulseTick = 0; pulseHaveRef = false; commandedSignedPWM = 0; return 0; }
  double e = targetKg - forceKg;
  bool push = e > 0;
  // Progress check every window, in BOTH zones: if the force has not moved,
  // the push (or retract) level was too low here -> raise it; else relax it.
  if (pulseTick == 0) {
    if (pulseHaveRef) {
      bool moved = fabs(forceKg - pulseRefF) >= PULSE_PROGRESS_KG;
      double& a = push ? pulseAdaptPush : pulseAdaptRet;
      a = moved ? fmax(0.0, a - 1.0) : fmin(PULSE_ADAPT_MAX, a + PULSE_ADAPT_STEP);
    }
    pulseRefF = forceKg; pulseHaveRef = true;
    pulseOnTicks = (fabs(e) >= 0.5 && forceKg < 40.0) ? 2 : 1;   // single 20 ms pulses at high force
  }
  int tickNow = pulseTick;
  pulseTick = (pulseTick + 1) % PULSE_WINDOW_TICKS;
  double lvl = push ? pushLevel(forceKg) : (RETRACT_PULSE_PWM + pulseAdaptRet);
  bool fine = push ? (e <= FINE_ZONE_KG) : (-e <= RETRACT_FINE_ZONE_KG);
  if (!fine) {
    // Coarse: PI output bounded between the break-free level (never stuck) and
    // break-free + KAPPA x distance to the fine zone (speed tapers on approach;
    // KAPPA shrinks with stiffness).
    double dist = push ? (e - FINE_ZONE_KG) : (-e - RETRACT_FINE_ZONE_KG);
    double kappa = COARSE_KAPPA * stiffScale(forceKg);
    double mag = fabs(u);
    if (mag < lvl) mag = lvl;
    if (mag > lvl + kappa * dist) mag = lvl + kappa * dist;
    if (mag > PI_PWM_MAX) mag = PI_PWM_MAX;
    return slewLimit(push ? mag : -mag);
  }
  commandedSignedPWM = 0;                                     // PI ramps from rest if it takes over again
  return (tickNow < pulseOnTicks) ? (push ? lvl : -lvl) : 0.0;
}

int applyControlDrive(double cmdSignedPWM) {
  applyMotorF(cmdSignedPWM);
  return appliedSignedPWM();   // what actually reached the driver (after gates/clamp)
}

// =====================================================================
//  SETPOINT / COMMAND HELPERS
// =====================================================================
bool setTarget(double kg) {
  if (kg < 0) kg = 0;
  if (kg > MAX_SETPOINT_KG) {
    Serial.print(F("Refused: target exceeds MAX_SETPOINT_KG = "));
    Serial.println(MAX_SETPOINT_KG);
    return false;
  }
  targetKg = kg;
  integral = 0; prevError = 0;
  return true;
}

void startRun(double kg) {
  if (mode == MODE_FAULT) { Serial.println(F("In FAULT. Reset with 'Z'.")); return; }
  if (estopActive)        { Serial.println(F("Refused: e-stop pressed.")); return; }
  if (!iSensorOk)         { Serial.println(F("Refused: current sensor fault (see self-test).")); return; }
  if (!tempOk)            { Serial.println(F("Refused: driver thermistor fault.")); return; }
  // A NEW run is refused while hot. A setpoint change during a run (the
  // dynamic test sends H every half-cycle) is allowed; the trip still applies.
  if (mode == MODE_IDLE && tempC >= TEMP_WARN_C) {
    Serial.print(F("Refused: driver at ")); Serial.print(tempC, 1);
    Serial.print(F(" C, let it cool below ")); Serial.print(TEMP_WARN_C, 0);
    Serial.println(F(" C."));
    return;
  }
  if (!setTarget(kg)) return;
  commandedSignedPWM = 0;          // start the slew ramp from rest (as rev 4)
  integral = 0;
  pulseTick = 0; pulseHaveRef = false; pulseAdaptPush = 0; pulseAdaptRet = 0;
  stallTimerRunning = false;
  regime = REG_APPROACH;
  enableMotor(true);
  mode = MODE_RUN;
  Serial.print(F(">> RUN: target ")); Serial.print(targetKg);
  Serial.println(F(" kg. Send 'S' to stop."));
}

void stopRun() {
  motorStop();
  enableMotor(false);
  commandedSignedPWM = 0;
  if (mode != MODE_FAULT) mode = MODE_IDLE;
  Serial.println(F(">> STOPPED (idle)."));
}

// =====================================================================
//  COMMAND INTERFACE
// =====================================================================
void printHelp() {
  Serial.println();
  Serial.println(F("============ LOAD-CELL FORCE-CONTROL RIG (rev 6.1) ============"));
  Serial.println(F(" COMMANDS"));
  Serial.print  (F("   H<kg>   Hold/seek a target force, e.g.  H30   (0.."));
  Serial.print(MAX_SETPOINT_KG, 0); Serial.println(F(" kg)"));
  Serial.println(F("   R       Read load cell, current, temperature, e-stop"));
  Serial.println(F("   +  / -  Jog forward / back ~1mm (open-loop)"));
  Serial.println(F("   B       Back off ~5mm (relative home)"));
  Serial.println(F("   S       Stop - motor off, return to idle"));
  Serial.println(F("   Z       Reset - clear a FAULT, then auto-retract ~5mm"));
  Serial.println(F("   Q       Beep the buzzer"));
  Serial.println(F("   W<0|1>  On HMI loss: 0 = hold, 1 = retract 5mm"));
  Serial.println(F("   E<0|1>  On reset after e-stop: 0 = clear only, 1 = retract 5mm"));
  Serial.println(F("   F<hz>   Telemetry rate to the touchscreen (1..50)"));
  Serial.println(F("   V<0..2> Buzzer volume: 0 = off, 1 = low, 2 = high"));
  Serial.println(F("   O1 / O0 Tare (zero) the reading now / clear the tare"));
  Serial.println(F("   ?       Show this help again"));
  Serial.println();
  Serial.println(F(" Telemetry (while running): F=force tgt=target cmd=virtual"));
  Serial.println(F(" pwm=applied I=motor current T=driver temp."));
  Serial.println(F(" cmd != pwm at low drive is the pulser, or the current clamp."));
  Serial.println(F(" Commands are tagged [USB] or [HMI] by source."));
  Serial.println(F(" SERIAL MONITOR: 115200 baud, line ending = Newline."));
  Serial.println(F(" SAFETY: keep a hand on the PHYSICAL e-stop at all times."));
  Serial.println(F("============================================================="));
}

// =====================================================================
//  HMI COMMAND VALIDATION  --  SAFETY CRITICAL
//  A line from the HMI is obeyed ONLY if it is exactly a well-formed
//  command: one known letter, and a numeric argument only for H/W/F/E.
//  The display's ROM boot log ("rst:0x1", "SPIWP:0xee") can then never
//  be executed as R or S.
// =====================================================================
bool hmiCommandIsValid(const String& s) {
  if (s.length() == 0) return false;

  char c = toupper(s.charAt(0));
  if (!strchr("HSZRBPQWFEVO+-?", c)) return false;

  String arg = s.substring(1);
  arg.trim();
  if (arg.length() == 0) return true;

  if (c != 'H' && c != 'W' && c != 'F' && c != 'E' && c != 'V' && c != 'O') return false;

  bool dotSeen = false;
  for (unsigned int i = 0; i < arg.length(); i++) {
    char a = arg.charAt(i);
    if (a == '.') {
      if (dotSeen) return false;
      dotSeen = true;
    } else if (!isdigit(a)) {
      return false;
    }
  }
  return true;
}

void handleCommandLine(const String& raw) {
  String s = raw; s.trim();
  if (s.length() == 0) return;

  if (s.startsWith("T F=")) return;   // our own telemetry echoed back

  Serial.print(cmdFromHmi ? F("[HMI] <- ") : F("[USB] <- "));
  Serial.println(s);

  char cmd = toupper(s.charAt(0));
  String arg = s.substring(1); arg.trim();

  switch (cmd) {
    case 'H': startRun(arg.toFloat()); break;
    case 'S': stopRun();               break;
    case 'Z': clearFault();            break;
    case 'R': reportForce();           break;
    case 'B':
      if (mode == MODE_FAULT) { Serial.println(F("In FAULT. Send 'Z' first.")); break; }
      if (estopActive)        { Serial.println(F("E-stop pressed. Release it first.")); break; }
      stopRun();
      doRelativeRetract();
      break;
    case '+': jogMove(+1);             break;
    case '-': jogMove(-1);             break;
    case 'Q': beep(120); break;

    case 'W':
      hmiLossAutoRetract = (arg.toInt() != 0);
      Serial.print(F(">> HMI-loss action: "));
      Serial.println(hmiLossAutoRetract ? F("RETRACT 5mm") : F("HOLD position"));
      break;

    // ---- E<0|1>: retract on reset after an e-stop ---------------------
    // Setting lives on the touchscreen; the HMI re-sends it on reconnect.
    case 'E':
      estopAutoRetract = (arg.toInt() != 0);
      Serial.print(F(">> After e-stop reset: "));
      Serial.println(estopAutoRetract ? F("RETRACT 5mm") : F("clear only"));
      break;

    // ---- O<0|1>: tare (rev 6.2) -----------------------------------------
    case 'O': doTare(arg.toInt() != 0); break;

    // ---- V<0|1|2>: buzzer volume (rev 6.1) -----------------------------
    case 'V': {
      long lv = arg.toInt();
      if (lv < 0) lv = 0;
      if (lv > 2) lv = 2;
      buzzerLevel = (uint8_t)lv;
      Serial.print(F(">> Buzzer: "));
      Serial.println(lv == 0 ? F("OFF") : (lv == 1 ? F("LOW") : F("HIGH")));
      break;
    }

    case 'F': {
      long hz = arg.toInt();
      if (hz < 1)  hz = 1;
      if (hz > 50) hz = 50;
      hmiTelemetryPeriodMs = 1000UL / (unsigned long)hz;
      Serial.print(F(">> Telemetry rate: ")); Serial.print(hz); Serial.println(F(" Hz"));
      break;
    }

    case 'P': break;                   // silent heartbeat
    case '?': printHelp();             break;
    default:  Serial.println(F("Unknown. Send '?' for help."));
  }
}

String usbBuf = "";
void pollUsbSerial() {
  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (usbBuf.length()) {
        cmdFromHmi = false;
        handleCommandLine(usbBuf);
        usbBuf = "";
      }
    } else {
      usbBuf += ch;
    }
  }
}

String hmiBuf = "";
void pollHmiSerial() {
  while (Serial1.available() > 0) {
    char ch = Serial1.read();
    if (ch == '\n' || ch == '\r') {
      if (hmiBuf.length()) {
        hmiBuf.trim();
        lastHmiRxMs = millis();
        if (hmiCommandIsValid(hmiBuf)) {
          cmdFromHmi = true;
          handleCommandLine(hmiBuf);
        }
        hmiBuf = "";
      }
    } else {
      if (hmiBuf.length() < 64) hmiBuf += ch;   // a runaway line cannot eat RAM
    }
  }
}

// =====================================================================
//  TELEMETRY (USB, human-readable)
// =====================================================================
unsigned long lastTelemetryMs = 0;
void emitTelemetry(double forceKg, int appliedSigned) {
  if (millis() - lastTelemetryMs < 250) return;
  lastTelemetryMs = millis();
  if (mode == MODE_IDLE) return;
  Serial.print(F("F=")); Serial.print(forceKg, 2);
  Serial.print(F("kg  tgt=")); Serial.print(targetKg, 2);
  Serial.print(F("  err=")); Serial.print(targetKg - forceKg, 2);
  Serial.print(F("  cmd=")); Serial.print(commandedSignedPWM);
  Serial.print(F("  pwm=")); Serial.print(appliedSigned);
  Serial.print(F("  I=")); Serial.print(iAmps, 2);
  Serial.print(F("A  T=")); if (tempOk) Serial.print(tempC, 1); else Serial.print(F("--"));
  Serial.print(F("C  regime=")); Serial.print(regime == REG_APPROACH ? F("APPR") : F("CONT"));
  if (mode == MODE_FAULT) { Serial.print(F("  FAULT:")); Serial.print(faultReason); }
  Serial.println();
}

// =====================================================================
//  HMI TELEMETRY  (Serial1 -> touchscreen)
//
//  T F=<kg> tgt=<kg> pwm=<signed> reg=APPR|CONT st=IDLE|RUN|FAULT lim=<bits>
//    I=<A, signed> Ipk=<A since boot> Td=<C, -999 = sensor fault>
//    es=<1 = e-stop pressed> df=<1 = driver FLT low> eot=<bits>
//    N=<raw ADS1220 counts behind F, 20 ms mean>          (rev 6.1)
//    tare=<kg subtracted from F>                          (rev 6.2)
//    [flt=<free text>]           <-- ALWAYS LAST: it contains spaces
//
//  lim / eot bitfields: bit0 = extend (push) end, bit1 = retract end.
//  Longest frame is ~145 chars; the HMI's line buffer is 200.
// =====================================================================
unsigned long lastHmiTxMs = 0;
void emitHmiTelemetry(double forceKg, int appliedSigned) {
  if (millis() - lastHmiTxMs < hmiTelemetryPeriodMs) return;
  lastHmiTxMs = millis();

  Serial1.print(F("T F="));    Serial1.print(forceKg, 2);
  Serial1.print(F(" tgt="));   Serial1.print(targetKg, 2);
  Serial1.print(F(" pwm="));   Serial1.print(appliedSigned);
  Serial1.print(F(" reg="));   Serial1.print(regime == REG_APPROACH ? F("APPR") : F("CONT"));
  Serial1.print(F(" st="));
  Serial1.print(mode == MODE_FAULT ? F("FAULT")
                                   : (mode == MODE_RUN ? F("RUN") : F("IDLE")));
  Serial1.print(F(" lim="));
  Serial1.print((limitPushHit ? 1 : 0) | (limitRetractHit ? 2 : 0));
  Serial1.print(F(" I="));     Serial1.print(iAmps, 2);
  Serial1.print(F(" Ipk="));   Serial1.print(iPeakA, 1);
  Serial1.print(F(" Td="));    Serial1.print(tempOk ? tempC : -999.0f, 1);
  Serial1.print(F(" es="));    Serial1.print(estopActive ? 1 : 0);
  Serial1.print(F(" df="));    Serial1.print(driverFaultLive ? 1 : 0);
  Serial1.print(F(" eot="));   Serial1.print((eotPush ? 1 : 0) | (eotRetract ? 2 : 0));
  Serial1.print(F(" N="));     Serial1.print(adsLastCounts);     // rev 6.1
  Serial1.print(F(" tare="));  Serial1.print(tareOffsetKg, 3);   // rev 6.2

  if (mode == MODE_FAULT) {            // keep flt LAST
    Serial1.print(F(" flt="));
    Serial1.print(faultReason);
  }
  Serial1.println();
}

// =====================================================================
//  HMI WATCHDOG  --  SAFETY CRITICAL (unchanged from rev 4)
// =====================================================================
void hmiWatchdog() {
  if (mode != MODE_RUN) return;
  if (lastHmiRxMs == 0) return;
  if (millis() - lastHmiRxMs < HMI_WATCHDOG_MS) return;

  Serial.println(F("!! HMI WATCHDOG: touchscreen silent during a run."));
  stopRun();

  if (hmiLossAutoRetract) {
    Serial.println(F("   Auto-retracting to unload the specimen."));
    doRelativeRetract();
  } else {
    Serial.println(F("   Holding position (motor off)."));
  }
  alarmPattern();
  lastHmiRxMs = millis();
}

void reportHmiLink() {
  bool present = (lastHmiRxMs != 0) && (millis() - lastHmiRxMs < HMI_PRESENT_MS);

  if (present != hmiWasPresent) {
    hmiWasPresent = present;
    Serial.println(present ? F("[HMI] touchscreen CONNECTED")
                           : F("[HMI] touchscreen LOST (no heartbeat)"));
    return;
  }

  static bool warnedNeverSeen = false;
  if (!warnedNeverSeen && lastHmiRxMs == 0 && millis() > 8000) {
    warnedNeverSeen = true;
    Serial.println(F("[HMI] no heartbeat after 8 s - display not connected?"));
    Serial.println(F("      check: TX/RX crossed, shared GND, and 'USB CDC On Boot'"));
    Serial.println(F("      enabled on the display."));
  }
}

// =====================================================================
//  POWER-ON SELF TEST
//  Returns true only if the CRITICAL items pass: ADC, load cell, current
//  sensor, thermistor. E-stop and driver are reported, not failed: a
//  pressed e-stop at boot is a legitimate state.
// =====================================================================
bool powerOnSelfTest(bool adcOk) {
  bool allOk = true;

  Serial.println();
  Serial.println(F("============== POWER-ON SELF TEST (rev 6) =============="));

  // ---- ADS1220 ----------------------------------------------------
  if (pwmOk) Serial.println(F(" [ OK ] Motor PWM      : timer running"));
  else      { Serial.println(F(" [FAIL] Motor PWM      : timer setup failed - the motor cannot be driven")); allOk = false; }
  if (adcOk) {
    Serial.print(F(" [ OK ] ADS1220 ADC    : registers verified, "));
    Serial.println(ADS_RATIOMETRIC ? F("RATIOMETRIC reference") : F("internal 2.048 V reference"));
  } else {
    Serial.println(F(" [FAIL] ADS1220 ADC    : no response - check SPI wiring / CS / power"));
    allOk = false;
  }

  // ---- Load cell --------------------------------------------------
  bool drdySeen = false;
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)SAMPLE_TIMEOUT_MS * 2) {
    if (adsDataReady()) { drdySeen = true; break; }
  }
  double f = readForceKgFiltered();
  bool cellOk = adcOk && drdySeen && (fabs(f) <= CELL_RATING_KG);
  if (cellOk) {
    Serial.print(F(" [ OK ] Load cell      : reading "));
    Serial.print(f, 2);
    Serial.println(F(" kg (within rating)"));
  } else if (!drdySeen) {
    Serial.println(F(" [FAIL] Load cell      : no DRDY - ADC not converting"));
    allOk = false;
  } else {
    Serial.print(F(" [FAIL] Load cell      : reading "));
    Serial.print(f, 2);
    Serial.println(F(" kg OUT OF RANGE - check cell wiring / calibration"));
    allOk = false;
  }
#if CAL_IS_PROVISIONAL
  Serial.println(F(" [WARN] Calibration    : PROVISIONAL (scaled for ratiometric mode)."));
  Serial.println(F("        Recalibrate with known masses before loads above ~20 kg."));
#endif

  // ---- Current sensor ----------------------------------------------
  if (iSensorOk) {
    Serial.print(F(" [ OK ] Current sensor : zero at "));
    Serial.print(iZeroMv, 0); Serial.println(F(" mV"));
  } else {
    Serial.print(F(" [FAIL] Current sensor : zero at "));
    Serial.print(iZeroMv, 0);
    Serial.println(F(" mV, expected ~1650 - check ACS773 supply / A0"));
    allOk = false;
  }

  // ---- Thermistor --------------------------------------------------
  if (tempOk) {
    Serial.print(F(" [ OK ] Driver temp    : "));
    Serial.print(tempC, 1); Serial.println(F(" C"));
  } else {
    Serial.print(F(" [FAIL] Driver temp    : "));
    Serial.print(tempRawMv, 0);
    Serial.println(tempRawMv > NTC_OPEN_MV ? F(" mV - thermistor OPEN") : F(" mV - thermistor SHORTED"));
    allOk = false;
  }

  // ---- E-stop and driver (informational) ----------------------------
  bool es = (digitalRead(EMER_SENSE_PIN) == HIGH);
  Serial.print(F(" [INFO] E-stop         : "));
  Serial.println(es ? F("PRESSED (or wire open) - motor power is off")
                    : F("released"));
  Serial.print(F(" [INFO] Motor driver   : G2 18v25, PWM "));
  Serial.print(MOTOR_PWM_FREQ_HZ / 1000); Serial.print(F(" kHz, FLT "));
  Serial.println(digitalRead(MOTOR_FAULT_PIN) == LOW ? F("LOW (fault or no motor power)") : F("clear"));
  Serial.println(F(" [INFO] Limit switches : not fitted - using actuator's internal ends"));

  // ---- Touchscreen link ---------------------------------------------
  Serial.print(F(" [INFO] Touchscreen    : Serial1 open @ "));
  Serial.print(HMI_BAUD);
  Serial.println(F(" baud, TX=D1 RX=D0"));

  beep(200);
  Serial.println(F(" [ OK ] Buzzer / LED   : power-on beep sent (D4), LED on D2/D3"));

  Serial.print(F(" [ OK ] Tare           : "));
  Serial.print(tareOffsetKg, 3);
  Serial.println(F(" kg (from flash; O1 = tare, O0 = clear)"));

  Serial.println(F("========================================================"));
  if (allOk) Serial.println(F(" >> ALL COMPONENTS INITIALISED OK. System ready."));
  else       Serial.println(F(" >> SELF-TEST FAILED. Do NOT run until resolved."));
  Serial.println();
  return allOk;
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  // Outputs to a safe state FIRST: motor off, DIR low, buzzer off.
  pinMode(MOTOR_DIR_PIN, OUTPUT);
  digitalWrite(MOTOR_DIR_PIN, LOW);
  pwmBegin();
  pwmWrite(0);

  buzzerBegin();                   buzzerWrite(false);
  pinMode(LED_RED_PIN, OUTPUT);    digitalWrite(LED_RED_PIN, HIGH);   // red until ready
  pinMode(LED_GREEN_PIN, OUTPUT);  digitalWrite(LED_GREEN_PIN, LOW);
  loadTare();                      // rev 6.2: zero offset from flash

  Serial.begin(115200);
  unsigned long tSerial = millis();
  while (!Serial && (millis() - tSerial < 2000)) { }

  Serial1.begin(HMI_BAUD, SERIAL_8N1, HMI_RX_PIN, HMI_TX_PIN);

  Serial.println();
  Serial.println(F("Booting load-cell force-control rig (rev 5, PCB rev 2)..."));

  // Safety inputs (internal pull-ups: no external ones on the PCB).
  pinMode(EMER_SENSE_PIN,  INPUT_PULLUP);
  pinMode(MOTOR_FAULT_PIN, INPUT_PULLUP);   // per the schematic note
  estopActive   = (digitalRead(EMER_SENSE_PIN) == HIGH);
  estopRawLast  = estopActive;
  estopChangedMs = millis();

  // Analog inputs: full-scale ~3.1 V (11 dB attenuation).
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(LIMIT_PUSH_PIN, INPUT_PULLUP);
  pinMode(LIMIT_RETRACT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(LIMIT_PUSH_PIN), isrLimitPush, CHANGE);
  attachInterrupt(digitalPinToInterrupt(LIMIT_RETRACT_PIN), isrLimitRetract, CHANGE);
  limitPushHit    = (digitalRead(LIMIT_PUSH_PIN) == LOW);
  limitRetractHit = (digitalRead(LIMIT_RETRACT_PIN) == LOW);

  pinMode(ADS1220_CS_PIN, OUTPUT);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  pinMode(ADS1220_DRDY_PIN, INPUT);
  SPI.begin();
  bool adcOk = adsInit();

  currentZeroAtBoot();
  for (int i = 0; i < 10; i++) sampleTemperature();

  systemHealthy = powerOnSelfTest(adcOk);
  if (!systemHealthy) alarmPattern();

  // Pressed at boot: latch it like any other press, so the HMI shows the
  // overlay and a reset is required once it is released.
  if (estopActive) {
    Serial.println(F("!! E-stop is PRESSED at boot."));
    enterFault(FC_ESTOP, "E-STOP");
  }

  printHelp();
  lastControlMs = millis();
  motorOffSince = millis();
}

// =====================================================================
//  MAIN LOOP  (fixed-rate control)
//  Pipeline:  safety inputs -> sensors -> force -> compute -> slew ->
//             pulse -> apply -> end-of-travel -> telemetry
// =====================================================================
void loop() {
  adsPoll();                 // REV 6: collect every 350 SPS conversion
  pollUsbSerial();
  pollHmiSerial();
  pollSafetyInputs();        // every pass: e-stop must not wait for a tick
  updateLeds();

  unsigned long now = millis();
  if (now - lastControlMs < (unsigned long)CONTROL_PERIOD_MS) return;
  double dt = (now - lastControlMs) / 1000.0;
  lastControlMs = now;

  sampleCurrent();
  sampleTemperature();
  temperatureCheck();

  double force = readForceKgFiltered();
  if (overloadCheck(force)) {
    // Keep talking during a fault: that is when the display most needs it.
    emitHmiTelemetry(force, 0);
    reportHmiLink();
    return;
  }

  int appliedSigned = 0;

  switch (mode) {
    case MODE_IDLE:
    case MODE_FAULT:
      motorStop();
      commandedSignedPWM = 0;
      commandingMotion   = false;
      break;

    case MODE_RUN: {
      double drive = controlStep(force, dt);
      commandingMotion = (drive != 0) || (pulseTick != 0);
      appliedSigned    = applyControlDrive(drive);
      stallCheck(force);
      break;
    }
  }

  eotCheck(appliedSigned);

  emitTelemetry(force, appliedSigned);
  emitHmiTelemetry(force, appliedSigned);
  reportHmiLink();
  hmiWatchdog();
}
