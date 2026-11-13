/* =====================================================================
 *  FORCE RIG — CALIBRATION & CHARACTERISATION HARNESS   V1
 *  For Force Rig PCB rev 2 (Pololu G2 18v25, ACS773 current sensor,
 *  driver NTC, e-stop sense, ratiometric ADS1220).
 *
 *  Companion workbook:  calibration/Force_Rig_Calibration_Workbook.xlsx
 *  Load-cell constants: calibration/Load_Cell_Calibration_Data.xlsx (UTM session)
 * =====================================================================
 *
 *  WHAT THIS IS
 *  ------------
 *  A standalone sketch that runs the characterisation campaign for the
 *  assembled rig. Every worksheet T0..T12 in the workbook has the SAME
 *  number here: menu "2" fills sheet "T2_...", and so on.
 *
 *  Not the operational firmware. It has no touchscreen link, logs raw
 *  counts as well as kg, and can log at the full control rate. The control
 *  law, limits and protective functions are the SAME as
 *  Force_Control_Rig_V4 (rig firmware rev 6), so the numbers describe the machine as run.
 *
 *  HOW RESULTS GET INTO THE WORKBOOK
 *  ---------------------------------
 *  At the end of every test the sketch asks you to CLEAR the serial
 *  monitor and press ENTER. It then prints ONLY the result rows,
 *  tab-separated, with no headers or markers. Press the monitor's copy
 *  button and paste into the green cell the sketch names. Each value lands
 *  in its own cell; no Text-to-Columns step. Press ENTER again for the menu.
 *    - Serial monitor: 115200 baud, line ending = Newline, TIMESTAMPS OFF.
 *    - "R" reprints the last result block if you missed it.
 *
 *  LONG RECORDS (warm-up, creep) — why they no longer freeze
 *  ---------------------------------------------------------
 *    - Samples go into ONE fixed table sized at compile time. Nothing grows
 *      while a test runs (no String, no malloc), so memory cannot run out.
 *    - Live progress lines are DROPPED, not waited for, if the PC stops
 *      reading (USB idle / sleep / monitor closed). Printing can therefore
 *      never stall the test; the data is safe in the table.
 *    - Each logged point is the AVERAGE of every conversion in its interval,
 *      so a 10 s interval is both quieter and smaller than fast single reads.
 *    Still: disable PC sleep and USB selective suspend for long records.
 *
 *  SAFETY
 *  ------
 *  This sketch drives the actuator. Keep a hand on the physical E-stop.
 *  Type X and ENTER at any time to abort the running test: the motor stops
 *  and (unless the e-stop is pressed) the actuator retracts.
 *
 *  BOARD: Arduino Nano ESP32. Pin names use D/A labels, so they are right
 *  whatever Tools > Pin Numbering is set to.
 * ===================================================================== */

#include <SPI.h>
#include <math.h>
#include <stdarg.h>
#include <esp_arduino_version.h>

#define FW_VERSION "Harness V1"

// =====================================================================
//  PINS  (controller PCB V2.1 — identical to the rig firmware rev 6.2)
// =====================================================================
#define ADS1220_CS_PIN     D10
#define ADS1220_DRDY_PIN   D9
#define MOTOR_PWM_PIN      D6
#define MOTOR_DIR_PIN      D5
#define MOTOR_FAULT_PIN    A2    // G2 FLT, open drain, LOW = fault
#define CURRENT_SENSE_PIN  A0    // ACS773 VIOUT
#define THERMISTOR_PIN     A3    // 10k NTC to GND, 10k to 3V3
#define EMER_SENSE_PIN     A1    // LOW = e-stop released, HIGH = pressed
#define BUZZER_PIN         D4
#define LED_RED_PIN        D2
#define LED_GREEN_PIN      D3
#define MOTOR_DIR_PUSH     HIGH
#define MOTOR_DIR_RETRACT  LOW

// =====================================================================
//  LOAD-CELL CALIBRATION   kg = SLOPE * counts + INTERCEPT
//  Copy these from the workbook sheet LC_Reference (rows "Slope k" and
//  "Intercept c"), which in turn come from the UTM session in
//  Load_Cell_Calibration_Data_V1.xlsx. Use the SAME values in the rig
//  firmware. Test 0 prints what is flashed so the workbook can check it.
//  Menu K changes them for one session without reflashing.
// =====================================================================
#define ADS_RATIOMETRIC 1          // MUST match the rig firmware and the UTM session
double CAL_SLOPE_KG_PER_COUNT = 0.000058327734; // UTM 2026-10-15, rounds 1-3, linear 0-100 kg (LC_Reference)
double CAL_INTERCEPT_KG       = 0.145367338;    // UTM 2026-10-15, rounds 1-3, linear 0-100 kg (LC_Reference)

// =====================================================================
//  LIMITS AND CONTROL — identical to Force_Control_Rig_V4 (rev 6)
// =====================================================================
const double CELL_RATING_KG    = 100.0;
const double OVERLOAD_TRIP_KG  = 95.0;
double       setpointCapKg     = 90.0;   // working range F_N (90 % of the 100 kg load cell); menu C changes it
const double CONTACT_ENTER_KG  = 1.5;
const double CONTACT_EXIT_KG   = 0.7;
const double STALL_DELTA_KG    = 0.3;
const unsigned long STALL_TIMEOUT_MS    = 4000;
const unsigned long APPROACH_TIMEOUT_MS = 30000;

// PI force control (2026-10-16). The integral finds the push the actuator
// needs at any load by itself. With the G2 at 20 kHz the force rises at
// 100-300 kg/s, so the measurement must be fast (ADS1220 at 350 SPS,
// averaged per 20 ms tick) or ANY controller overshoots by kilograms.
// Inside the hold band the motor stops and the integral freezes: the
// self-locking leadscrew holds the force, so there is no hunting.
// Gains chosen on a model fitted to Test 11; verify with Test 9.
double Kp = 6.0;                        // PWM per kg of error      (as used for Tests 2-11, 2026-10-19)
double Ki = 10.0;                       // PWM per kg*s             (Test 9: 25 hunts)
const double PI_PWM_MAX        = 100;   // controller output limit (speed at high force)
const double I_CLAMP_KGS       = 150;   // integral clamp, kg*s
const double HOLD_BELOW_KG     = 0.05;  // stop once force is within 0.05 kg BELOW target...
const double HOLD_ABOVE_KG     = 0.25;  // ...and retract only if more than 0.25 kg ABOVE it
const int    PWM_MAX           = 200;   // absolute drive limit (open-loop retract/jog use 150-180)
const int    PWM_MIN_MOVE      = 40;
const int    PWM_APPROACH      = 45;
const double DEADBAND_KG       = 0.4;   // SETTLED band used by the tests (not the hold band)
const double PWM_SLEW_PER_TICK = 6;
// FINE ZONE (last FINE_ZONE_KG before the target) -- the old rig's pulser,
// with a load-dependent level. The leadscrew sticks until the push reaches a
// break-free level, then jumps; continuous drive here either does nothing or
// jumps past the target before the reading catches up. Short pulses move it a
// little and then wait for the reading. Pulse level = break-free line (Test 11
// running line + ~10 PWM static margin; matches Test 9: ~43 PWM at 19 kg),
// raised automatically while pulses fail to move the force.
const double FINE_ZONE_KG       = 2.0;   // push: pulses within this of the target (Test 9: 1 kg was crossed in < 1 tick)
const double RETRACT_FINE_ZONE_KG = 5.0; // retract: pulses within this (continuous retract runs away: the load helps it)
const double COARSE_KAPPA       = 4.0;   // coarse drive <= break-free level + KAPPA x distance to the fine zone (PWM/kg)
const int    PULSE_WINDOW_TICKS = 10;    // 200 ms: pulse, then let the reading settle
// Push level = Test 11 "keeps moving" line + static (break-free) margin.
// The margin shrinks with force because the load path stiffens (Test 11:
// the same PWM moves the force ~5x faster at 50 kg than at 7 kg; Test 9:
// +10 PWM was right at 20 kg but jumped 1-3 kg per pulse at 80 kg).
const double RUN_PWM_AT_0       = 18.3;  // Test 11
const double RUN_PWM_PER_KG     = 0.775; // Test 11
const double STATIC_MARGIN_PWM  = 10.0;  // right at 20 kg (Test 9); x exp(-(F-30)/STIFF_SCALE_KG) above 30 kg
const double STATIC_MARGIN_KNEE_KG = 30.0;
const double STIFF_SCALE_KG     = 35.0;
const double RETRACT_PULSE_PWM  = 40.0;  // retract pulse level (+ adapt); the load helps
const double PULSE_PROGRESS_KG  = 0.03;  // less change than this over a window = "did not move"
const double PULSE_ADAPT_STEP   = 3.0;   // PWM added per window without progress
const double PULSE_ADAPT_MAX    = 30.0;
const int    CONTROL_PERIOD_MS = 20;
const double FORCE_FILTER_ALPHA = 0.5;  // on 20 ms averages of 350 SPS conversions

// Settling criterion used by every test: inside the deadband continuously
// for this long. Settling TIME is measured to ENTRY into the band.
const unsigned long SETTLE_CONFIRM_MS = 2000;
const unsigned long SETTLE_TIMEOUT_MS = 90000;

const double RETRACT_DIST_MM    = 5.0;
const double RETRACT_SPEED_MM_S = 5.0;
const int    RETRACT_PWM        = 180;
const double JOG_DIST_MM        = 1.0;
const double JOG_SPEED_MM_S     = 5.0;
const int    JOG_PWM            = 150;
const double RETRACT_FORCE_RISE_ABORT_KG = 2.0;

// ---- Motor current (ACS773LCB-050B, 3.3 V) ----
const float ACS_SENS_MV_PER_A   = 26.4f;
const int   CURRENT_SIGN        = -1;   // Test 11: push read negative with +1
const float ACS_ZERO_NOMINAL_MV = 1650.0f;
const float ACS_ZERO_TOL_MV     = 200.0f;
const float CURRENT_FILTER_ALPHA = 0.30f;
const float I_CLAMP_A           = 6.0f;
const float OC_TRIP_A_DEFAULT   = 8.0f;
const unsigned long OC_TRIP_MS  = 500;
const float EOT_CURRENT_A       = 0.15f;
const int   EOT_MIN_PWM         = 40;
const unsigned long EOT_MS      = 400;

// ---- Driver temperature (10k NTC, B3950) ----
const float NTC_R0_OHMS = 10000.0f, NTC_BETA = 3950.0f, NTC_T0_K = 298.15f;
const float NTC_RFIX_OHMS = 10000.0f, NTC_SUPPLY_MV = 3300.0f;
const float NTC_OPEN_MV = 3050.0f, NTC_SHORT_MV = 40.0f;
const float TEMP_FILTER_ALPHA = 0.05f;
const float TEMP_WARN_C = 70.0f, TEMP_TRIP_C_DEFAULT = 85.0f, TEMP_RESET_C = 70.0f;

// ---- E-stop / driver fault ----
const unsigned long ESTOP_DEBOUNCE_MS = 20, FLT_DEBOUNCE_MS = 5;
const unsigned long FLT_MASK_AFTER_ESTOP_MS = 1000, FLT_MASK_BOOT_MS = 1000;

// ---- PWM ----
const uint32_t MOTOR_PWM_FREQ_HZ = 20000;
const uint8_t  MOTOR_PWM_BITS = 10, MOTOR_PWM_CHANNEL = 0;   // 10-bit: 4x finer than 8-bit; values stay on the 0-255 scale.
                                                                // (11-bit at 20 kHz needs a 41 MHz timer clock and can fail silently.)

// =====================================================================
//  ADS1220
// =====================================================================
#define ADS1220_CMD_RESET 0x06
#define ADS1220_CMD_START 0x08
#define ADS1220_CMD_RDATA 0x10
#define ADS1220_CMD_RREG  0x20
#define ADS1220_CMD_WREG  0x40
#define ADS1220_REG0_VAL  0x0E   // AIN0/AIN1, gain 128, PGA on
#define ADS1220_REG1_VAL  0x74   // 350 SPS (turbo, DR 011), continuous -- was 0x14 (40 SPS)
#if ADS_RATIOMETRIC
#define ADS1220_REG2_VAL  0x40   // VREF = REFP0/REFN0; 50/60 Hz FIR off (only valid at 20 SPS):
#else                            // the 20 ms per-tick average rejects 50 Hz instead
#define ADS1220_REG2_VAL  0x00   // VREF = internal 2.048 V
#endif
#define ADS1220_REG3_VAL  0x00
const int  SAMPLE_TIMEOUT_MS = 50;
const long ADC_FS_POS = 8388607L, ADC_FS_NEG = -8388608L;
SPISettings adsSPI(1000000, MSBFIRST, SPI_MODE1);

// =====================================================================
//  RESULT TABLE — the ONLY storage that grows during a test, and it is
//  fixed-size. 1600 rows x 12 columns of float = 77 KB, allocated once.
//  float holds integers up to 16.7 M exactly, so 24-bit counts are safe.
// =====================================================================
#define MAX_ROWS 1600
#define MAX_COLS 12
float   tbl[MAX_ROWS][MAX_COLS];
int     tblRows = 0, tblCols = 0;
uint8_t tblDec[MAX_COLS];
char    tblSheet[40] = "";
char    tblCell[48]  = "";

// Fault codes. The workbook's T12 sheet maps these numbers to names.
enum FaultCode {
  FC_NONE = 0, FC_OVERLOAD = 1, FC_ESTOP = 2, FC_DRIVER = 3, FC_OVERTEMP = 4,
  FC_OVERCURRENT = 5, FC_NO_CURRENT = 6, FC_EOT = 7, FC_STALL = 8,
  FC_APPROACH = 9, FC_SENSOR = 10
};
const char* FAULT_NAMES[] = {
  "none", "OVERLOAD", "E-STOP", "DRIVER FAULT", "DRIVER OVER-TEMP",
  "OVERCURRENT", "NO MOTOR CURRENT", "END OF TRAVEL", "STALL",
  "APPROACH TIMEOUT", "SENSOR FAULT"
};

// =====================================================================
//  STATE
// =====================================================================
int    faultCode = FC_NONE;
unsigned long faultAtMs = 0;
bool   abortReq = false;

double ovlTripKg = OVERLOAD_TRIP_KG;     // T12 lowers these temporarily
float  ocTripA   = OC_TRIP_A_DEFAULT;
float  tempTripC = TEMP_TRIP_C_DEFAULT;

enum Regime { REG_APPROACH, REG_CONTACT };
Regime regime = REG_APPROACH;
double targetKg = 0.0;
bool   running = false;            // closed-loop hold active
bool   motorEnabled = false;
int    currentPWM = 0, currentDirSign = +1;
double commandedSignedPWM = 0;   double integral = 0;
int    pulseTick = 0, pulseOnTicks = 1; bool pulseHaveRef = false;
double pulseRefF = 0, pulseAdaptPush = 0, pulseAdaptRet = 0;
bool   commandingMotion = false;

double forceFiltered = 0.0;  bool forceSeeded = false;
bool retractRecovering = false;   // set only by faultRetract()
long   lastCounts = 0;

unsigned long stallSince = 0; bool stallTimerRunning = false; double stallRefForce = 0;

float iZeroMv = ACS_ZERO_NOMINAL_MV, iRawA = 0, iAmps = 0, iPeakBootA = 0;
bool  iSensorOk = false;
int   iLimitPwm = 255;
unsigned long ocSince = 0; bool ocTiming = false;
unsigned long motorOffSince = 0;

bool  eotPush = false, eotRetract = false;
int   eotDir = 0; unsigned long eotSince = 0;
int   lastAppliedForEot = 0, prevAppliedForEot = 0;

float tempC = 25.0f, tempRawMv = 0; bool tempOk = false, tempSeeded = false;

bool  estopActive = false, estopRawLast = false;
unsigned long estopRawSince = 0, estopChangedMs = 0;
bool  driverFaultLive = false, fltLowTiming = false; unsigned long fltLowSince = 0;

unsigned long lastTickMs = 0;
unsigned long liveDropped = 0;

// Last result printer, for "R".
void (*lastPrinter)() = nullptr;
char lastSheet[40] = "", lastCell[48] = "";
int  lastRows = 0;

// Types used in function signatures. Declared up here because the Arduino
// IDE inserts auto-generated prototypes above the first function.
struct Tick { double f; long counts; float amps; float temp; int pwm; };
struct SeekResult {
  double force = NAN; unsigned long settleMs = 0; double overshootPct = 0;
  float iPeak = 0; float iMeanMoving = 0; float temp = NAN;
};

// forward declarations
void motorStop();
void enterFault(int code);
void printMenu();

// =====================================================================
//  ADS1220 DRIVER
// =====================================================================
void adsWriteReg(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(adsSPI); digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_WREG | (reg << 2)); SPI.transfer(val);
  digitalWrite(ADS1220_CS_PIN, HIGH); SPI.endTransaction();
}
uint8_t adsReadReg(uint8_t reg) {
  SPI.beginTransaction(adsSPI); digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RREG | (reg << 2)); uint8_t v = SPI.transfer(0x00);
  digitalWrite(ADS1220_CS_PIN, HIGH); SPI.endTransaction(); return v;
}
void adsCommand(uint8_t cmd) {
  SPI.beginTransaction(adsSPI); digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(cmd); digitalWrite(ADS1220_CS_PIN, HIGH); SPI.endTransaction();
}
long adsReadRaw() {
  SPI.beginTransaction(adsSPI); digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RDATA);
  uint8_t b0 = SPI.transfer(0), b1 = SPI.transfer(0), b2 = SPI.transfer(0);
  digitalWrite(ADS1220_CS_PIN, HIGH); SPI.endTransaction();
  long raw = ((long)b0 << 16) | ((long)b1 << 8) | b2;
  if (raw & 0x800000) raw |= 0xFF000000;
  return raw;
}
bool adsInit() {
  adsCommand(ADS1220_CMD_RESET); delay(100);
  adsWriteReg(0, ADS1220_REG0_VAL); adsWriteReg(1, ADS1220_REG1_VAL);
  adsWriteReg(2, ADS1220_REG2_VAL); adsWriteReg(3, ADS1220_REG3_VAL);
  bool ok = adsReadReg(0) == ADS1220_REG0_VAL && adsReadReg(1) == ADS1220_REG1_VAL &&
            adsReadReg(2) == ADS1220_REG2_VAL && adsReadReg(3) == ADS1220_REG3_VAL;
  adsCommand(ADS1220_CMD_START); delay(100);
  return ok;
}
bool adsDataReady() { return digitalRead(ADS1220_DRDY_PIN) == LOW; }

// ---- Conversion accumulator. The ADC runs at 350 SPS; adsPoll() collects
// every conversion and readRawCounts() returns their AVERAGE since the
// last call (one control tick = 20 ms = ~7 conversions). A 20 ms average
// spans exactly one 50 Hz mains period, so it rejects mains pickup the way
// the old 40 SPS 50/60 Hz filter did, at a fraction of the delay.
double adsAccSum = 0; int adsAccN = 0; bool adsAccSat = false; long adsAccSatVal = 0;
unsigned long adsAccStartMs = 0;
void adsAccReset() { adsAccSum = 0; adsAccN = 0; adsAccSat = false; }
void adsPoll() {
  if (!adsDataReady()) return;
  long c = adsReadRaw();
  if (adsAccN == 0) adsAccStartMs = millis();
  if (c >= ADC_FS_POS || c <= ADC_FS_NEG) { adsAccSat = true; adsAccSatVal = c; }
  adsAccSum += (double)c; adsAccN++;
}
long readRawCounts() {
  adsPoll();
  if (adsAccN > 0 && millis() - adsAccStartMs > 40) adsAccReset();   // nobody read for a while: stale
  unsigned long t0 = millis();
  while (adsAccN == 0 && millis() - t0 < (unsigned long)SAMPLE_TIMEOUT_MS) adsPoll();
  if (adsAccN > 0) lastCounts = adsAccSat ? adsAccSatVal : lround(adsAccSum / adsAccN);
  adsAccReset();
  return lastCounts;
}
double countsToKg(double c) { return CAL_SLOPE_KG_PER_COUNT * c + CAL_INTERCEPT_KG; }
bool   isSaturated(long c)  { return c >= ADC_FS_POS || c <= ADC_FS_NEG; }

double readForceKgFiltered() {
  double raw = countsToKg((double)readRawCounts());
  if (!forceSeeded) { forceFiltered = raw; forceSeeded = true; }
  else forceFiltered += FORCE_FILTER_ALPHA * (raw - forceFiltered);
  return forceFiltered;
}

// =====================================================================
//  PWM (LEDC, 20 kHz)
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
// Duty on the 0-255 scale used everywhere; written at 11-bit resolution.
void pwmWriteF(double duty) {
  if (duty < 0) duty = 0; if (duty > 255) duty = 255;
  uint32_t d = (uint32_t)lround(duty * ((1 << MOTOR_PWM_BITS) - 1) / 255.0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MOTOR_PWM_PIN, d);
#else
  ledcWrite(MOTOR_PWM_CHANNEL, d);
#endif
}
void pwmWrite(int duty) { pwmWriteF((double)duty); }

// =====================================================================
//  ANALOG SENSORS
// =====================================================================
float readMvAvg(uint8_t pin, int n) {
  uint32_t s = 0; for (int i = 0; i < n; i++) s += analogReadMilliVolts(pin);
  return (float)s / n;
}
void currentZero() {
  motorStop(); delay(50);
  iZeroMv = readMvAvg(CURRENT_SENSE_PIN, 256);
  iSensorOk = fabsf(iZeroMv - ACS_ZERO_NOMINAL_MV) < ACS_ZERO_TOL_MV;
  iAmps = 0; iRawA = 0;
}
void sampleCurrent() {
  float mv = readMvAvg(CURRENT_SENSE_PIN, 8);
  iRawA = CURRENT_SIGN * (mv - iZeroMv) / ACS_SENS_MV_PER_A;
  iAmps += CURRENT_FILTER_ALPHA * (iRawA - iAmps);
  if (fabsf(iAmps) > iPeakBootA) iPeakBootA = fabsf(iAmps);
  if (currentPWM == 0 && !estopActive && millis() - motorOffSince > 500 &&
      fabsf(mv - iZeroMv) < 60.0f) iZeroMv += 0.01f * (mv - iZeroMv);
  if (fabsf(iAmps) > I_CLAMP_A) iLimitPwm -= 4; else iLimitPwm += 2;
  if (iLimitPwm < PWM_MIN_MOVE) iLimitPwm = PWM_MIN_MOVE;
  if (iLimitPwm > 255) iLimitPwm = 255;
  if (fabsf(iAmps) > ocTripA) {
    if (!ocTiming) { ocTiming = true; ocSince = millis(); }
    else if (millis() - ocSince > OC_TRIP_MS) enterFault(FC_OVERCURRENT);
  } else ocTiming = false;
}
void sampleTemperature() {
  float mv = readMvAvg(THERMISTOR_PIN, 4); tempRawMv = mv;
  if (mv > NTC_OPEN_MV || mv < NTC_SHORT_MV) { tempOk = false; return; }
  float r = NTC_RFIX_OHMS * mv / (NTC_SUPPLY_MV - mv);
  float t = 1.0f / (1.0f / NTC_T0_K + logf(r / NTC_R0_OHMS) / NTC_BETA) - 273.15f;
  if (!tempSeeded) { tempC = t; tempSeeded = true; } else tempC += TEMP_FILTER_ALPHA * (t - tempC);
  tempOk = true;
  if (tempC >= tempTripC) enterFault(FC_OVERTEMP);
}

// =====================================================================
//  BUZZER / LEDS
// =====================================================================
void beep(int ms = 150) { digitalWrite(BUZZER_PIN, HIGH); delay(ms); digitalWrite(BUZZER_PIN, LOW); }
void alarmPattern() { for (int i = 0; i < 3; i++) { beep(150); delay(100); } }
void updateLeds() {
  unsigned long t = millis(); bool r = false, g = false;
  if (estopActive)              r = (t / 125) % 2;
  else if (faultCode)           r = true;
  else if (motorEnabled)        g = (t / 500) % 2;
  else                          g = true;
  digitalWrite(LED_RED_PIN, r); digitalWrite(LED_GREEN_PIN, g);
}

// =====================================================================
//  MOTOR
// =====================================================================
void motorStop() {
  pwmWrite(0);
  if (currentPWM != 0) motorOffSince = millis();
  currentPWM = 0;
  if (estopActive) digitalWrite(MOTOR_DIR_PIN, LOW);
}
void enableMotor(bool en) { motorEnabled = en; if (!en) motorStop(); }

void applyMotorF(double signedPWM) {
  if (!motorEnabled || faultCode || estopActive) { motorStop(); return; }
  if (signedPWM > 0 && eotPush)    signedPWM = 0;
  if (signedPWM < 0 && eotRetract) signedPWM = 0;
  double mag = fabs(signedPWM);
  if (mag > PWM_MAX) mag = PWM_MAX;
  if (mag > iLimitPwm) mag = iLimitPwm;           // soft current clamp, as the rig
  if (mag < 0.1) { motorStop(); return; }
  digitalWrite(MOTOR_DIR_PIN, signedPWM > 0 ? MOTOR_DIR_PUSH : MOTOR_DIR_RETRACT);
  pwmWriteF(mag);
  currentPWM = (int)lround(mag); if (currentPWM < 1) currentPWM = 1;
  currentDirSign = signedPWM > 0 ? +1 : -1;
}
void applyMotor(int signedPWM) { applyMotorF((double)signedPWM); }
int appliedSigned() { return currentPWM * currentDirSign; }

void stopHold() {
  running = false; commandedSignedPWM = 0; enableMotor(false);
}

// =====================================================================
//  FAULTS
// =====================================================================
void enterFault(int code) {
  if (faultCode) return;                 // keep the FIRST cause
  faultCode = code; faultAtMs = millis();
  int pwmAtFault = appliedSigned();      // captured before the motor is stopped
  running = false; commandedSignedPWM = 0;
  motorStop(); enableMotor(false);
  Serial.print(F("\n!! FAULT: ")); Serial.println(FAULT_NAMES[code]);
  Serial.print(F("   At the trip: F=")); Serial.print(forceFiltered, 2);
  Serial.print(F(" kg  I=")); Serial.print(iAmps, 2);
  Serial.print(F(" A (raw ")); Serial.print(iRawA, 2);
  Serial.print(F(" A)  PWM=")); Serial.print(pwmAtFault);
  Serial.print(F("  Tdrv=")); if (tempOk) Serial.print(tempC, 1); else Serial.print(F("--"));
  Serial.println(F(" C"));
  Serial.println(F("   Motor off. Fix the cause, then send Z to clear."));
  alarmPattern();
}

bool clearFault(bool askRetract) {
  if (!faultCode) { Serial.println(F("   No fault latched.")); return true; }
  if (estopActive) { Serial.println(F("   Release the E-STOP first.")); return false; }
  if (digitalRead(MOTOR_FAULT_PIN) == LOW && millis() - estopChangedMs > FLT_MASK_AFTER_ESTOP_MS) {
    Serial.println(F("   Driver still reports a fault (FLT low).")); return false;
  }
  if (faultCode == FC_OVERTEMP && (!tempOk || tempC >= TEMP_RESET_C)) {
    Serial.println(F("   Driver still too hot.")); return false;
  }
  double f = readForceKgFiltered();
  if (fabs(f) > ovlTripKg) { Serial.println(F("   Still overloaded - M then - or b retracts with the fault latched.")); return false; }
  if (eotPush && eotRetract) { eotPush = eotRetract = false; }
  Serial.print(F("   Cleared: ")); Serial.println(FAULT_NAMES[faultCode]);
  faultCode = FC_NONE; ocTiming = false; stallTimerRunning = false;
  (void)askRetract;
  return true;
}

// =====================================================================
//  END OF TRAVEL (actuator's own end switches cut its power)
// =====================================================================
void declareEot(int dir) {
  if (dir > 0) eotPush = true; else eotRetract = true;
  motorStop();
  Serial.print(F("\n!! END OF TRAVEL: "));
  Serial.println(dir > 0 ? F("extend end") : F("retract end"));
  if (eotPush && eotRetract) enterFault(FC_NO_CURRENT);
  else if (running) enterFault(FC_EOT);
}
void eotCheck(int appliedNow) {
  int judged = lastAppliedForEot;
  bool steady = judged != 0 && prevAppliedForEot != 0 &&
                ((judged > 0) == (prevAppliedForEot > 0)) &&
                abs(judged) >= EOT_MIN_PWM && abs(prevAppliedForEot) >= EOT_MIN_PWM;
  prevAppliedForEot = lastAppliedForEot;
  lastAppliedForEot = appliedNow;
  if (!steady || estopActive) { eotDir = 0; return; }
  int dir = judged > 0 ? +1 : -1;
  if (fabsf(iRawA) < EOT_CURRENT_A) {
    if (eotDir != dir) { eotDir = dir; eotSince = millis(); }
    else if (millis() - eotSince > EOT_MS) { eotDir = 0; declareEot(dir); }
  } else {
    eotDir = 0;
    if (dir > 0) eotRetract = false; else eotPush = false;
  }
}

// =====================================================================
//  E-STOP AND DRIVER FAULT PIN  (polled continuously)
//  The e-stop is a FAULT only while the motor is enabled. With the motor
//  idle it is just a state, so tests can use it to remove motor power.
// =====================================================================
void pollSafetyInputs() {
  unsigned long now = millis();
  bool raw = digitalRead(EMER_SENSE_PIN) == HIGH;
  if (raw != estopRawLast) { estopRawLast = raw; estopRawSince = now; }
  if (raw != estopActive && now - estopRawSince >= ESTOP_DEBOUNCE_MS) {
    estopActive = raw; estopChangedMs = now;
    if (estopActive) {
      bool wasMoving = motorEnabled;
      motorStop();
      if (wasMoving) enterFault(FC_ESTOP);
    }
  }
  bool fltLow = digitalRead(MOTOR_FAULT_PIN) == LOW;
  bool masked = estopActive || now - estopChangedMs < FLT_MASK_AFTER_ESTOP_MS || now < FLT_MASK_BOOT_MS;
  driverFaultLive = fltLow && !masked;
  if (driverFaultLive) {
    if (!fltLowTiming) { fltLowTiming = true; fltLowSince = now; }
    else if (now - fltLowSince >= FLT_DEBOUNCE_MS) enterFault(FC_DRIVER);
  } else fltLowTiming = false;
}

// =====================================================================
//  CONSOLE INPUT  — non-blocking line reader; X aborts a running test
// =====================================================================
char lineBuf[64]; size_t lineLen = 0;

// Returns true when a full line (possibly empty) has arrived in lineBuf.
bool readLineNB() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { lineBuf[lineLen] = 0; lineLen = 0; return true; }
    if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
  return false;
}
// During a test: any line starting with X requests an abort.
void pollAbort() {
  if (readLineNB()) {
    if (lineBuf[0] == 'x' || lineBuf[0] == 'X') {
      abortReq = true;
      Serial.println(F("\n>> ABORT requested."));
    }
  }
}
// Keep the machine safe while waiting for the operator.
void idleService() {
  pollSafetyInputs(); updateLeds(); adsPoll();
}
// Blocking line read that keeps safety running. Returns false on "X".
bool readLineBlocking() {
  while (true) {
    idleService();
    if (readLineNB()) {
      return !(lineBuf[0] == 'x' || lineBuf[0] == 'X') || lineBuf[1] != 0;
    }
    delay(2);
  }
}
void trimLine() {
  char* s = lineBuf; while (*s == ' ') s++;
  size_t n = strlen(s); while (n && s[n - 1] == ' ') s[--n] = 0;
  memmove(lineBuf, s, n + 1);
}
bool pausePrompt(const char* prompt) {
  Serial.print(F("\n>> ")); Serial.print(prompt);
  Serial.println(F("\n   Press ENTER when ready (X + ENTER to abort)."));
  bool ok = readLineBlocking();
  if (!ok) Serial.println(F("   Aborted."));
  return ok;
}
bool confirmYes(const char* prompt) {
  Serial.print(F("\n>> ")); Serial.print(prompt);
  Serial.println(F("  Type YES + ENTER to continue, anything else aborts."));
  readLineBlocking(); trimLine();
  for (char* p = lineBuf; *p; p++) *p = toupper(*p);
  bool ok = strcmp(lineBuf, "YES") == 0;
  if (!ok) Serial.println(F("   Aborted."));
  return ok;
}
// Returns false if the operator typed X.
bool askDouble(const char* prompt, double dflt, double& out) {
  Serial.print(F(">> ")); Serial.print(prompt);
  Serial.print(F(" [")); Serial.print(dflt, 2); Serial.print(F("]: "));
  if (!readLineBlocking()) { Serial.println(F("X")); return false; }
  trimLine(); Serial.println(lineBuf);
  out = lineBuf[0] ? atof(lineBuf) : dflt;
  return true;
}
bool askLong(const char* prompt, long dflt, long& out) {
  Serial.print(F(">> ")); Serial.print(prompt);
  Serial.print(F(" [")); Serial.print(dflt); Serial.print(F("]: "));
  if (!readLineBlocking()) { Serial.println(F("X")); return false; }
  trimLine(); Serial.println(lineBuf);
  out = lineBuf[0] ? atol(lineBuf) : dflt;
  return true;
}
void rule() { Serial.println(F("-------------------------------------------------------------")); }

// Live progress line. DROPPED rather than waited for if the PC is not
// reading, so a sleeping USB link can never stall a long record.
// The Nano ESP32's USB transmit buffer is small (often 64 bytes), so a
// line can be longer than the whole buffer. A line is sent when it fits OR
// the buffer is empty (the PC has drained everything sent so far; txFree
// tracks the largest free space ever seen = empty). A reading PC drains the
// buffer within ~1 ms, so we wait up to 3 ms for that -- short enough not
// to disturb the 20 ms control tick. A PC that is not reading costs 3 ms
// per line and the line is dropped.
void live(const char* fmt, ...) {
  static int txFree = 0;
  char b[160];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  size_t n = strlen(b);
  unsigned long w0 = micros();
  while (true) {
    int avail = Serial.availableForWrite();
    if (avail > txFree) txFree = avail;
    if (avail > 0 && (avail >= (int)(n + 2) || avail >= txFree)) break;
    if (micros() - w0 > 3000UL) { liveDropped++; return; }
  }
  Serial.write((const uint8_t*)b, n); Serial.write((const uint8_t*)"\r\n", 2);
}

// =====================================================================
//  RESULT TABLE AND THE PASTE PROTOCOL
// =====================================================================
void tblBegin(const char* sheet, const char* cell, int cols, const uint8_t* decs) {
  tblRows = 0; tblCols = cols;
  for (int i = 0; i < cols; i++) tblDec[i] = decs[i];
  strncpy(tblSheet, sheet, sizeof(tblSheet) - 1); tblSheet[sizeof(tblSheet) - 1] = 0;
  strncpy(tblCell, cell, sizeof(tblCell) - 1);    tblCell[sizeof(tblCell) - 1] = 0;
}
bool tblAdd(const float* v) {
  if (tblRows >= MAX_ROWS) return false;
  for (int i = 0; i < tblCols; i++) tbl[tblRows][i] = v[i];
  tblRows++;
  return true;
}
void tblPrinter() {
  for (int r = 0; r < tblRows; r++) {
    for (int c = 0; c < tblCols; c++) {
      if (c) Serial.write('\t');
      float v = tbl[r][c];
      if (!isnan(v)) Serial.print(v, tblDec[c]);   // NaN prints as an empty cell
    }
    Serial.write('\n');
  }
}
// Prompts the operator to clear the monitor, prints ONLY the rows, waits.
void pasteProtocol(const char* sheet, const char* cell, int rows, void (*printer)()) {
  if (rows <= 0) { Serial.println(F("\n   (no rows to paste)")); return; }
  lastPrinter = printer; lastRows = rows;
  strncpy(lastSheet, sheet, sizeof(lastSheet) - 1);
  strncpy(lastCell, cell, sizeof(lastCell) - 1);
  rule();
  Serial.print(F("RESULTS READY: ")); Serial.print(rows);
  Serial.print(F(" row(s) for sheet ")); Serial.println(sheet);
  Serial.print(F("PASTE AT: ")); Serial.println(cell);
  rule();
  Serial.println(F(" 1. CLEAR the serial monitor now (bin / clear icon)."));
  Serial.println(F(" 2. Timestamps must be OFF."));
  Serial.println(F(" 3. Press ENTER: only the data rows will be printed."));
  Serial.println(F(" 4. Press the monitor's COPY button, select that cell, paste."));
  Serial.println(F(" 5. Press ENTER again to return to the menu."));
  Serial.println(F("    (X + ENTER skips printing; R at the menu reprints.)"));
  if (!readLineBlocking()) { Serial.println(F("   skipped - R reprints it.")); return; }
  printer();
  readLineBlocking();             // silent: nothing may follow the rows
}
void pasteTable() { pasteProtocol(tblSheet, tblCell, tblRows, tblPrinter); }

// =====================================================================
//  CONTROL — identical law to Force_Control_Rig_V4 (rev 6)
// =====================================================================
double computeControl(double f) {
  if (targetKg < CONTACT_ENTER_KG)          regime = REG_CONTACT;
  else if (regime == REG_APPROACH) { if (f > CONTACT_ENTER_KG) regime = REG_CONTACT; }
  else                             { if (f < CONTACT_EXIT_KG)  regime = REG_APPROACH; }
  if (regime == REG_APPROACH) { integral = 0; return +PWM_APPROACH; }
  double e = targetKg - f;
  if (e <= HOLD_BELOW_KG && e >= -HOLD_ABOVE_KG) return 0;     // hold: motor off, integral frozen
  if (e * integral < 0) integral = 0;                          // crossed the target: drop old windup
  double u = Kp * e + Ki * integral;
  bool intoLimit = fabs(u) >= PI_PWM_MAX && ((u > 0) == (e > 0));
  if (!intoLimit && (e > FINE_ZONE_KG || e < -RETRACT_FINE_ZONE_KG)) {   // anti-windup; frozen in the fine zones
    integral += e * (CONTROL_PERIOD_MS / 1000.0);
    if (integral >  I_CLAMP_KGS) integral =  I_CLAMP_KGS;
    if (integral < -I_CLAMP_KGS) integral = -I_CLAMP_KGS;
  }
  if (u >  PI_PWM_MAX) u =  PI_PWM_MAX;
  if (u < -PI_PWM_MAX) u = -PI_PWM_MAX;
  return u;
}
double slewLimit(double desired) {            // only increases are rate-limited
  bool sameDir = (desired >= 0) == (commandedSignedPWM >= 0);
  bool rampUp  = sameDir && fabs(desired) > fabs(commandedSignedPWM);
  if (rampUp) {
    if (desired > commandedSignedPWM) { commandedSignedPWM += PWM_SLEW_PER_TICK; if (commandedSignedPWM > desired) commandedSignedPWM = desired; }
    else                              { commandedSignedPWM -= PWM_SLEW_PER_TICK; if (commandedSignedPWM < desired) commandedSignedPWM = desired; }
  } else commandedSignedPWM = desired;
  return commandedSignedPWM;
}
double stiffScale(double f) { return fmin(1.0, exp(-(fmax(f, 0.0) - 10.0) / STIFF_SCALE_KG)); }
double pushLevel(double f)  { return RUN_PWM_AT_0 + RUN_PWM_PER_KG * fmax(f, 0.0)
                                     + STATIC_MARGIN_PWM * fmin(1.0, exp(-(fmax(f, 0.0) - STATIC_MARGIN_KNEE_KG) / STIFF_SCALE_KG))
                                     + pulseAdaptPush; }
// One control tick: PI (slew-limited) outside the fine zone, bounded pulses inside it.
double controlStep(double f) {
  double u = computeControl(f);
  if (regime == REG_APPROACH) { pulseTick = 0; pulseHaveRef = false; return slewLimit(u); }
  if (u == 0) { pulseTick = 0; pulseHaveRef = false; commandedSignedPWM = 0; return 0; }   // hold band
  double e = targetKg - f;
  bool push = e > 0;
  // Progress check every window, in BOTH zones: if the force has not moved,
  // the push (or retract) level was too low here -> raise it; else relax it.
  if (pulseTick == 0) {
    if (pulseHaveRef) {
      bool moved = fabs(f - pulseRefF) >= PULSE_PROGRESS_KG;
      double& a = push ? pulseAdaptPush : pulseAdaptRet;
      a = moved ? fmax(0.0, a - 1.0) : fmin(PULSE_ADAPT_MAX, a + PULSE_ADAPT_STEP);
    }
    pulseRefF = f; pulseHaveRef = true;
    pulseOnTicks = (fabs(e) >= 0.5 && f < 40.0) ? 2 : 1;   // single 20 ms pulses at high force
  }
  int tickNow = pulseTick;
  pulseTick = (pulseTick + 1) % PULSE_WINDOW_TICKS;
  double lvl = push ? pushLevel(f) : (RETRACT_PULSE_PWM + pulseAdaptRet);
  bool fine = push ? (e <= FINE_ZONE_KG) : (-e <= RETRACT_FINE_ZONE_KG);
  if (!fine) {
    // Coarse: PI output bounded between the break-free level (never stuck) and
    // break-free + KAPPA x distance to the fine zone (speed tapers on approach;
    // KAPPA shrinks with stiffness).
    double dist = push ? (e - FINE_ZONE_KG) : (-e - RETRACT_FINE_ZONE_KG);
    double kappa = COARSE_KAPPA * stiffScale(f);
    double mag = fabs(u);
    if (mag < lvl) mag = lvl;
    if (mag > lvl + kappa * dist) mag = lvl + kappa * dist;
    if (mag > PI_PWM_MAX) mag = PI_PWM_MAX;
    return slewLimit(push ? mag : -mag);
  }
  commandedSignedPWM = 0;                                     // PI ramps from rest if it takes over again
  return (tickNow < pulseOnTicks) ? (push ? lvl : -lvl) : 0.0;
}
void stallCheck(double f) {
  if (!commandingMotion) { stallTimerRunning = false; return; }
  // Within 1 kg of the target the PI may legitimately creep slowly.
  if (regime == REG_CONTACT && fabs(targetKg - f) <= 1.0) { stallTimerRunning = false; return; }
  unsigned long lim = regime == REG_APPROACH ? APPROACH_TIMEOUT_MS : STALL_TIMEOUT_MS;
  if (!stallTimerRunning) { stallTimerRunning = true; stallSince = millis(); stallRefForce = f; return; }
  if (fabs(f - stallRefForce) > STALL_DELTA_KG) stallTimerRunning = false;
  else if (millis() - stallSince > lim) enterFault(regime == REG_APPROACH ? FC_APPROACH : FC_STALL);
}
void startHold(double kg) {
  targetKg = kg; regime = REG_APPROACH; commandedSignedPWM = 0; integral = 0;
  pulseTick = 0; pulseHaveRef = false; pulseAdaptPush = 0; pulseAdaptRet = 0;
  stallTimerRunning = false; enableMotor(true); running = true;
}

// =====================================================================
//  THE TICK — every test advances through this, one control period at a
//  time. Reads force, current and temperature, runs every protective
//  check, runs the control law if a hold is active.
//  Returns false if a fault or an abort is active.
// =====================================================================

bool tick(Tick& t) {
  while (millis() - lastTickMs < (unsigned long)CONTROL_PERIOD_MS) {
    pollSafetyInputs(); pollAbort(); updateLeds(); adsPoll();
    if (faultCode || abortReq) break;
  }
  lastTickMs = millis();
  sampleCurrent();
  sampleTemperature();
  t.f = readForceKgFiltered();
  t.counts = lastCounts;
  if (fabs(t.f) > ovlTripKg && !retractRecovering) enterFault(FC_OVERLOAD);   // retracting can only relieve it
  if (running && !faultCode) {
    double s = controlStep(t.f);
    commandingMotion = (s != 0) || (pulseTick != 0);
    applyMotorF(s);
    stallCheck(t.f);
  } else commandingMotion = false;
  eotCheck(appliedSigned());
  t.amps = iAmps; t.temp = tempOk ? tempC : NAN; t.pwm = appliedSigned();
  return !(faultCode || abortReq);
}

// Keep holding (or idling) for ms, servicing everything.
bool holdFor(unsigned long ms) {
  Tick t; unsigned long t0 = millis();
  while (millis() - t0 < ms) if (!tick(t)) return false;
  return true;
}

// =====================================================================
//  MOTION
// =====================================================================

// Drive to kg and wait for settling. 0 = settled, 1 = timeout, 2 = fault, 3 = abort.
int seekAndSettle(double kg, SeekResult& r, bool verbose = true) {
  startHold(kg);
  Tick t; unsigned long t0 = millis(), inBand = 0, lastPrint = 0;
  double peak = 0; float iPk = 0, iSum = 0; long iN = 0;
  while (millis() - t0 < SETTLE_TIMEOUT_MS) {
    if (!tick(t)) return faultCode ? 2 : 3;
    if (t.f > peak) peak = t.f;
    if (fabsf(t.amps) > iPk) iPk = fabsf(t.amps);
    if (t.pwm != 0) { iSum += fabsf(t.amps); iN++; }
    if (fabs(t.f - kg) <= DEADBAND_KG) {
      if (!inBand) inBand = millis();
      if (millis() - inBand >= SETTLE_CONFIRM_MS) {
        r.force = t.f; r.settleMs = inBand - t0;
        r.overshootPct = (peak > kg && kg > 0) ? (peak - kg) / kg * 100.0 : 0.0;
        r.iPeak = iPk; r.iMeanMoving = iN ? iSum / iN : 0; r.temp = t.temp;
        if (verbose) live("   settled %.3f kg in %.2f s  (Ipk %.2f A, Tdrv %.1f C)",
                          t.f, r.settleMs / 1000.0, iPk, t.temp);
        return 0;
      }
    } else inBand = 0;
    if (verbose && millis() - lastPrint > 1000) {
      lastPrint = millis();
      live("   ... %.2f kg   I %.2f A   pwm %d", t.f, t.amps, t.pwm);
    }
  }
  Serial.println(F("   !! did not settle within the timeout"));
  return 1;
}

// Open-loop timed move. Returns false on fault/abort.
bool openLoopMove(int pwm, double mm, double mmps, const char* what) {
  unsigned long dur = (unsigned long)(mm / mmps * 1000.0);
  live("   %s", what);
  running = false; enableMotor(true);
  Tick t; unsigned long t0 = millis(); bool ok = true;
  double f0 = readForceKgFiltered();
  while (millis() - t0 < dur) {
    if (pwm > 0 && eotPush) break;
    if (pwm < 0 && eotRetract) break;
    applyMotor(pwm);
    if (!tick(t)) { ok = false; break; }
    if (pwm < 0 && t.f - f0 > RETRACT_FORCE_RISE_ABORT_KG) { live("   force rose while retracting - stopped"); break; }
    if (pwm > 0 && t.f > setpointCapKg) { live("   reached the setpoint cap - stopped"); break; }
  }
  motorStop(); enableMotor(false); commandedSignedPWM = 0;
  return ok;
}
bool doRetract() {
  if (estopActive) return false;
  bool savedAbort = abortReq; abortReq = false;          // a retract after X must run
  bool ok = openLoopMove(-RETRACT_PWM, RETRACT_DIST_MM, RETRACT_SPEED_MM_S, "retracting ~5 mm");
  abortReq = savedAbort || abortReq;                     // keep an X typed DURING the retract
  return ok;
}
bool doJog(int dir) {
  return openLoopMove(dir > 0 ? +JOG_PWM : -JOG_PWM, JOG_DIST_MM, JOG_SPEED_MM_S,
                      dir > 0 ? "jog extend ~1 mm" : "jog retract ~1 mm");
}

// Common test prologue/epilogue.
bool testReady(bool drivesActuator) {
  abortReq = false;
  if (faultCode) { Serial.print(F("   Fault latched: ")); Serial.println(FAULT_NAMES[faultCode]);
                   Serial.println(F("   Send Z to clear it first.")); return false; }
  if (drivesActuator) {
    if (estopActive) { Serial.println(F("   E-stop is pressed - release it first.")); return false; }
    if (!iSensorOk)  { Serial.println(F("   Current-sensor zero out of range - see L.")); return false; }
    if (!tempOk)     { Serial.println(F("   Thermistor fault - see L.")); return false; }
    if (tempC >= TEMP_WARN_C) { Serial.println(F("   Driver above 70 C - let it cool.")); return false; }
    if (!confirmYes("This test DRIVES THE ACTUATOR. Fixture clear, hand on the E-stop?")) return false;
  }
  return true;
}
void testEnd(bool retract) {
  bool aborted = abortReq;
  stopHold();
  if (retract && !faultCode && !estopActive) doRetract();
  abortReq = false;
  if (aborted) Serial.println(F("   Test aborted by operator; rows so far are kept."));
  if (faultCode) Serial.println(F("   Test stopped by a fault; rows so far are kept."));
  if (liveDropped) { Serial.print(F("   (")); Serial.print(liveDropped);
                     Serial.println(F(" live lines were dropped while the PC was not reading; no data lost.)"));
                     liveDropped = 0; }
}

// Ask for up to maxN setpoints. Returns count, 0 if aborted.
int askSetpoints(double* sp, int maxN, long dfltN, const double* dflt) {
  long n;
  if (!askLong("How many setpoints", dfltN, n)) return 0;
  if (n < 1 || n > maxN) { Serial.print(F("   1 to ")); Serial.print(maxN); Serial.println(F(" please.")); return 0; }
  for (int i = 0; i < n; i++) {
    char p[32]; snprintf(p, sizeof(p), "Setpoint %d (kg)", i + 1);
    if (!askDouble(p, i < 7 ? dflt[i] : 10, sp[i])) return 0;
    if (sp[i] > setpointCapKg) { Serial.print(F("   capped at ")); Serial.println(setpointCapKg); sp[i] = setpointCapKg; }
    if (sp[i] < 1) sp[i] = 1;
  }
  Serial.print(F("   Setpoints (kg):"));
  for (int i = 0; i < n; i++) { Serial.print(i ? F(", ") : F(" ")); Serial.print(sp[i], 1); }
  Serial.println();
  return (int)n;
}
const double DEFAULT_SP[7] = { 5, 10, 20, 30, 40, 50, 60 };

// =====================================================================
//  P — PRELOAD three times to the setpoint cap (ISO 7500-1 Cl. 6.4.3)
// =====================================================================
bool doPreload(bool standalone) {
  if (standalone && !testReady(true)) return false;
  Serial.print(F("\nPreloading 3x to ")); Serial.print(setpointCapKg, 1); Serial.println(F(" kg."));
  for (int i = 1; i <= 3; i++) {
    live("-- preload %d/3", i);
    if (!doRetract()) return false;
    SeekResult r;
    if (seekAndSettle(setpointCapKg, r, false) != 0) { stopHold(); return false; }
    if (!holdFor(3000)) { stopHold(); return false; }
    stopHold();
  }
  doRetract();
  Serial.println(F("   Preload complete."));
  return true;
}
bool offerPreload() {
  long p; if (!askLong("Preload 3x to the cap first? 1=yes 0=no", 1, p)) return false;
  return p ? doPreload(false) : true;
}

// =====================================================================
//  TEST 0 — SESSION RECORD  ->  Setup!B36 (one column, 17 rows)
// =====================================================================
float t0DriverTemp = NAN;
void t0Printer() {
  Serial.println(FW_VERSION);
  Serial.println(CAL_SLOPE_KG_PER_COUNT, 12);
  Serial.println(CAL_INTERCEPT_KG, 9);
  Serial.println(ADS_RATIOMETRIC ? "ratiometric" : "internal 2.048 V");
  Serial.println(MOTOR_PWM_FREQ_HZ);
  if (!isnan(t0DriverTemp)) Serial.println(t0DriverTemp, 1); else Serial.println();
  Serial.println(iZeroMv, 0);
  Serial.println(ACS_SENS_MV_PER_A, 1);
  Serial.println(setpointCapKg, 1);
  Serial.println(Kp, 2);
  Serial.println(DEADBAND_KG, 2);
  Serial.println(OVERLOAD_TRIP_KG, 1);
  Serial.println(OC_TRIP_A_DEFAULT, 1);
  Serial.println(I_CLAMP_A, 1);
  Serial.println(TEMP_WARN_C, 1);
  Serial.println(TEMP_TRIP_C_DEFAULT, 1);
  Serial.println(SETTLE_CONFIRM_MS);
}
void t0PiPrinter() {
  Serial.print(F("Ki (PWM per kg*s)\t"));                  Serial.println(Ki, 2);
  Serial.print(F("PI output limit (PWM, 0-255 scale)\t"));  Serial.println(PI_PWM_MAX, 0);
  Serial.print(F("Hold band below target (kg)\t"));         Serial.println(HOLD_BELOW_KG, 2);
  Serial.print(F("Hold band above target (kg)\t"));         Serial.println(HOLD_ABOVE_KG, 2);
  Serial.print(F("Control period (ms)\t"));                 Serial.println(CONTROL_PERIOD_MS);
  Serial.print(F("ADC rate (SPS, averaged per period)\t")); Serial.println(350);
  Serial.print(F("PWM bits (at MOTOR_PWM_FREQ_HZ)\t"));     Serial.println(MOTOR_PWM_BITS);
}
void test0_session() {
  rule(); Serial.println(F("TEST 0 - SESSION RECORD  (sheet Setup)")); rule();
  Serial.println(F("Records what is flashed and the conditions at the start of the"));
  Serial.println(F("session. Run it FIRST, from a cold rig (off for 2 h or more): the"));
  Serial.println(F("driver temperature is then a good proxy for ambient."));
  abortReq = false;
  float s = 0; int n = 0;
  for (int i = 0; i < 50; i++) { sampleTemperature(); if (tempOk) { s += tempC; n++; } delay(10); }
  t0DriverTemp = n ? s / n : NAN;
  Serial.print(F("   Driver temperature now: ")); Serial.print(t0DriverTemp, 1); Serial.println(F(" C"));
  pasteProtocol("Setup", "Setup!B36  (17 rows, one column)", 17, t0Printer);
  Serial.println(F("\nSecond block: the PI settings, with their labels (for the paper)."));
  pasteProtocol("Setup", "Setup!D36  (7 rows, two columns: label, value)", 7, t0PiPrinter);
}

// =====================================================================
//  TEST 1 — RESOLUTION AND NOISE FLOOR  ->  T1_Noise_Resolution!A13
//  Four conditions separate electrical noise (motor power on/off) from
//  mechanical noise (free vs seated against a preload).
// =====================================================================
bool burst(long n, float* row, int cond) {
  double sum = 0, sumSq = 0, sumC = 0, sumCSq = 0, mn = 1e9, mx = -1e9, iS = 0, iSq = 0;
  long sat = 0;
  for (long i = 0; i < n; i++) {
    unsigned long w0 = millis();                  // one sample = one 20 ms average, as the
    while (millis() - w0 < (unsigned long)CONTROL_PERIOD_MS) adsPoll();   // control loop sees it
    long c = readRawCounts();
    if (isSaturated(c)) sat++;
    double kg = countsToKg((double)c);
    sum += kg; sumSq += kg * kg; sumC += c; sumCSq += (double)c * c;
    if (kg < mn) mn = kg; if (kg > mx) mx = kg;
    float mv = readMvAvg(CURRENT_SENSE_PIN, 4);
    double a = CURRENT_SIGN * (mv - iZeroMv) / ACS_SENS_MV_PER_A;
    iS += a; iSq += a * a;
    pollSafetyInputs(); pollAbort(); updateLeds();
    if (abortReq) return false;
    if (i % 400 == 0) live("   %ld / %ld samples", i, n);
  }
  double mean = sum / n, sd = n > 1 ? sqrt(fmax(0, (sumSq - n * mean * mean) / (n - 1))) : 0;
  double mc = sumC / n,  sdc = n > 1 ? sqrt(fmax(0, (sumCSq - n * mc * mc) / (n - 1))) : 0;
  double mi = iS / n,    sdi = n > 1 ? sqrt(fmax(0, (iSq - n * mi * mi) / (n - 1))) : 0;
  sampleTemperature();
  float v[12] = { (float)cond, (float)n, (float)mean, (float)sd, (float)mn, (float)mx,
                  (float)(mx - mn), (float)mc, (float)sdc, (float)mi, (float)sdi,
                  tempOk ? tempC : NAN };
  for (int i = 0; i < 12; i++) row[i] = v[i];
  live("   cond %d: mean %.4f kg  sd %.5f kg  p-p %.4f kg  I %.3f A", cond, mean, sd, mx - mn, mi);
  if (sat) Serial.println(F("   !! ADC SATURATED in this burst - result invalid (wiring/reference)."));
  return true;
}
bool waitEstop(bool wantPressed) {
  while (estopActive != wantPressed) {
    Serial.println(wantPressed ? F("   E-stop must be PRESSED for this condition. Press it, then ENTER.")
                               : F("   E-stop must be RELEASED for this condition. Release it, then ENTER."));
    if (!readLineBlocking()) return false;
    for (int i = 0; i < 10; i++) { pollSafetyInputs(); delay(5); }
  }
  return true;
}
void test1_noise() {
  rule(); Serial.println(F("TEST 1 - RESOLUTION AND NOISE FLOOR  (sheet T1_Noise_Resolution)")); rule();
  Serial.println(F("Order of conditions (rows are always printed 1-4):"));
  Serial.println(F("  1  Idle, no load, motor power OFF (e-stop pressed)"));
  Serial.println(F("  2  Idle, no load, motor power ON  (e-stop released, PWM 0)"));
  Serial.println(F("  4  Seated on a light preload, motor power ON, loop stopped"));
  Serial.println(F("  3  Seated on the same preload, motor power OFF (e-stop pressed)"));
  Serial.println(F("Do not touch the rig while a condition is measuring."));
  abortReq = false;
  if (faultCode) { Serial.println(F("   Clear the fault with Z first.")); return; }
  long n; double pre;
  if (!askLong("Samples per condition (~45 per second, each a 20 ms average)", 2000, n)) return;
  if (n < 50) n = 50; if (n > 20000) n = 20000;
  if (!askDouble("Preload for conditions 3 and 4 (kg)", 5.0, pre)) return;
  uint8_t dec[12] = { 0, 0, 5, 5, 5, 5, 5, 1, 1, 3, 3, 1 };
  tblBegin("T1_Noise_Resolution", "T1_Noise_Resolution!A13", 12, dec);
  float rows[4][12]; bool have[4] = { false, false, false, false };

  // ---- condition 1
  Serial.println(F("\nCondition 1: no load, motor power OFF."));
  if (!pausePrompt("Remove any load and PRESS the e-stop.")) goto done;
  if (!waitEstop(true)) goto done;
  have[0] = burst(n, rows[0], 1); if (!have[0]) goto done;
  // ---- condition 2
  Serial.println(F("\nCondition 2: no load, motor power ON, PWM 0."));
  if (!pausePrompt("RELEASE the e-stop.")) goto done;
  if (!waitEstop(false)) goto done;
  delay(1500); currentZero();
  have[1] = burst(n, rows[1], 2); if (!have[1]) goto done;
  // ---- seat on the preload
  {
    Serial.println(F("\nSeating on the preload for conditions 4 and 3."));
    if (!confirmYes("The actuator will now drive to the preload. Fixture clear?")) goto done;
    SeekResult r;
    if (seekAndSettle(pre, r) != 0) { stopHold(); goto done; }
    stopHold();
    holdFor(1500);
  }
  Serial.println(F("\nCondition 4: seated, motor power ON, loop stopped."));
  have[3] = burst(n, rows[3], 4); if (!have[3]) goto done;
  Serial.println(F("\nCondition 3: seated, motor power OFF."));
  if (!pausePrompt("PRESS the e-stop (the load stays on: the leadscrew holds).")) goto done;
  if (!waitEstop(true)) goto done;
  have[2] = burst(n, rows[2], 3); if (!have[2]) goto done;
  Serial.println(F("\nRelease the e-stop; the actuator will then retract."));
  waitEstop(false);
done:
  for (int i = 0; i < 4; i++) if (have[i]) tblAdd(rows[i]);
  testEnd(!estopActive);
  pasteTable();
}

// =====================================================================
//  TEST 2 — COMMANDED vs ACHIEVED  ->  T2_Command_Accuracy (first empty row)
// =====================================================================
void test2_command() {
  rule(); Serial.println(F("TEST 2 - COMMANDED vs ACHIEVED FORCE  (sheet T2_Command_Accuracy)")); rule();
  Serial.println(F("Retracts to a common unloaded start before EVERY repetition, then"));
  Serial.println(F("drives to the setpoint and records the settled force."));
  if (!testReady(true)) return;
  double sp[10]; int n = askSetpoints(sp, 10, 6, DEFAULT_SP); if (!n) return;
  long reps; if (!askLong("Repetitions per setpoint", 5, reps)) return;
  if (reps < 1) reps = 1;
  Serial.print(F("   Plan: ")); Serial.print(n); Serial.print(F(" setpoint(s) x ")); Serial.print(reps);
  Serial.print(F(" rep(s) = ")); Serial.print(n * reps); Serial.println(F(" approaches.  X + ENTER aborts."));
  if (!offerPreload()) { testEnd(true); return; }
  uint8_t dec[8] = { 2, 0, 4, 0, 2, 2, 2, 1 };
  tblBegin("T2_Command_Accuracy", "T2_Command_Accuracy!A13 (or the first empty row)", 8, dec);
  for (int i = 0; i < n && !abortReq && !faultCode; i++) {
    for (int r = 1; r <= reps && !abortReq && !faultCode; r++) {
      live("\n-- setpoint %.2f kg, rep %d/%ld", sp[i], r, reps);
      if (!doRetract()) break;
      if (!holdFor(1500)) break;
      SeekResult s; int rc = seekAndSettle(sp[i], s);
      stopHold();
      if (rc == 2 || rc == 3) break;
      float v[8] = { (float)sp[i], (float)r, rc == 0 ? (float)s.force : NAN,
                     rc == 0 ? (float)s.settleMs : NAN, rc == 0 ? (float)s.overshootPct : NAN,
                     s.iPeak, s.iMeanMoving, s.temp };
      tblAdd(v);
    }
  }
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 3 — SHORT-TERM REPEATABILITY  ->  T3_Repeatability (first empty row)
// =====================================================================
void test3_repeat() {
  rule(); Serial.println(F("TEST 3 - SHORT-TERM REPEATABILITY  (sheet T3_Repeatability)")); rule();
  Serial.println(F("Repeated approaches to ONE setpoint with nothing disturbed between"));
  Serial.println(F("trials. Run once per setpoint; paste each run below the last."));
  if (!testReady(true)) return;
  double sp; long trials, full;
  if (!askDouble("Setpoint (kg)", 30, sp)) return;
  if (sp > setpointCapKg) sp = setpointCapKg;
  if (!askLong("Number of trials (10 minimum for a usable SD)", 10, trials)) return;
  if (!askLong("Full 5 mm retract between trials? 1=yes 0=1 mm jog back", 1, full)) return;
  if (!offerPreload()) { testEnd(true); return; }
  uint8_t dec[6] = { 2, 0, 4, 0, 2, 1 };
  tblBegin("T3_Repeatability", "T3_Repeatability!A13 (or the first empty row)", 6, dec);
  for (int r = 1; r <= trials && !abortReq && !faultCode; r++) {
    live("\n-- trial %d/%ld", r, trials);
    if (!(full ? doRetract() : doJog(-1))) break;
    if (!holdFor(1200)) break;
    SeekResult s; int rc = seekAndSettle(sp, s);
    stopHold();
    if (rc == 2 || rc == 3) break;
    float v[6] = { (float)sp, (float)r, rc == 0 ? (float)s.force : NAN,
                   rc == 0 ? (float)s.settleMs : NAN, s.iPeak, s.temp };
    tblAdd(v);
  }
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 4 — SEATING REPRODUCIBILITY / GAUGE R&R  ->  T4_Seating_GageRR
// =====================================================================
void test4_grr() {
  rule(); Serial.println(F("TEST 4 - SEATING REPRODUCIBILITY / GAUGE R&R  (sheet T4_Seating_GageRR)")); rule();
  Serial.println(F("The load path is fully RESEATED before every trial. Run once per"));
  Serial.println(F("operator with the SAME three setpoints; paste each run below the last."));
  if (!testReady(true)) return;
  long op; if (!askLong("Operator ID (1, 2, 3)", 1, op)) return;
  double sp[3]; const double d[7] = { 10, 30, 50, 0, 0, 0, 0 };
  int n = askSetpoints(sp, 3, 3, d); if (!n) return;
  long trials; if (!askLong("Trials per setpoint", 3, trials)) return;
  uint8_t dec[6] = { 0, 2, 0, 4, 0, 1 };
  tblBegin("T4_Seating_GageRR", "T4_Seating_GageRR!A13 (or the first empty row)", 6, dec);
  for (int r = 1; r <= trials && !abortReq && !faultCode; r++) {
    for (int i = 0; i < n && !abortReq && !faultCode; i++) {
      live("\n-- operator %ld, setpoint %.2f kg, trial %d", op, sp[i], r);
      if (!doRetract()) break;
      if (!pausePrompt("RESEAT the load path completely (remove and refit specimen/platen).")) { abortReq = true; break; }
      SeekResult s; int rc = seekAndSettle(sp[i], s);
      stopHold();
      if (rc == 2 || rc == 3) break;
      float v[6] = { (float)op, (float)sp[i], (float)r, rc == 0 ? (float)s.force : NAN,
                     rc == 0 ? (float)s.settleMs : NAN, s.temp };
      tblAdd(v);
    }
  }
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 5 — DIRECTION DEPENDENCE  ->  T5_Direction (first empty row)
// =====================================================================
void test5_direction() {
  rule(); Serial.println(F("TEST 5 - DIRECTION DEPENDENCE (BACKLASH + DEADBAND)  (sheet T5_Direction)")); rule();
  Serial.println(F("Each setpoint is reached from BELOW (from unloaded) and from ABOVE"));
  Serial.println(F("(after settling at 110 % of the setpoint)."));
  if (!testReady(true)) return;
  double sp[10]; const double d[7] = { 10, 30, 50, 0, 0, 0, 0 };
  int n = askSetpoints(sp, 10, 3, d); if (!n) return;
  long reps; if (!askLong("Repetitions per setpoint", 3, reps)) return;
  uint8_t dec[7] = { 2, 0, 4, 4, 2, 2, 1 };
  tblBegin("T5_Direction", "T5_Direction!A13 (or the first empty row)", 7, dec);
  for (int i = 0; i < n && !abortReq && !faultCode; i++) {
    double over = fmin(sp[i] * 1.10, setpointCapKg);
    for (int r = 1; r <= reps && !abortReq && !faultCode; r++) {
      float below = NAN, above = NAN, ib = NAN, ia = NAN; SeekResult s;
      live("\n-- %.1f kg from BELOW, rep %d", sp[i], r);
      if (!doRetract() || !holdFor(1200)) break;
      int rc = seekAndSettle(sp[i], s);
      if (rc == 2 || rc == 3) { stopHold(); break; }
      if (rc == 0) { below = s.force; ib = s.iPeak; }
      live("-- %.1f kg from ABOVE", sp[i]);
      SeekResult so; rc = seekAndSettle(over, so, false);
      if (rc == 0) { rc = seekAndSettle(sp[i], s); if (rc == 0) { above = s.force; ia = s.iPeak; } }
      stopHold();
      if (rc == 2 || rc == 3) break;
      float v[7] = { (float)sp[i], (float)r, below, above, ib, ia, tempOk ? tempC : NAN };
      tblAdd(v);
    }
  }
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TESTS 6, 7, 8 — LONG RECORDS (warm-up / creep with loop on / off)
//  Each logged point is the AVERAGE over its interval. Rows are fixed-size
//  in the table; live lines are dropped if the PC stops reading.
// =====================================================================
void longRecord(int testNo) {
  bool warm = testNo == 6, loopOn = testNo == 7;
  rule();
  if (warm) Serial.println(F("TEST 6 - WARM-UP AND ZERO DRIFT, UNLOADED, FROM COLD  (sheet T6_Warmup_Drift)"));
  else if (loopOn) Serial.println(F("TEST 7 - CREEP UNDER LOAD, CONTROL LOOP ON  (sheet T7_Creep_LoopOn)"));
  else Serial.println(F("TEST 8 - CREEP UNDER LOAD, LOOP STOPPED AFTER SETTLING  (sheet T8_Creep_LoopOff)"));
  rule();
  if (warm) {
    Serial.println(F("Start this within a minute of powering a COLD rig. Leave it unloaded"));
    Serial.println(F("and untouched. The sketch reports when force AND driver temperature"));
    Serial.println(F("have been stable for 10 minutes; that start time is the warm-up period."));
  } else {
    Serial.println(F("Run AFTER the warm-up period. Settles at the setpoint, then logs."));
    Serial.println(loopOn ? F("The loop keeps regulating: this is creep as a user sees it.")
                          : F("The loop stops; the leadscrew holds: mechanical relaxation only."));
  }
  if (!testReady(!warm)) return;
  long mins, every; double sp = 0, res = 0.05;
  if (!askLong("Duration (minutes)", warm ? 60 : 35, mins)) return;
  if (!askLong("Log interval (seconds, 5 minimum)", 10, every)) return;
  if (every < 5) every = 5;
  long rowsNeeded = mins * 60 / every + 1;
  if (rowsNeeded > MAX_ROWS) {
    Serial.print(F("   Too many rows (")); Serial.print(rowsNeeded);
    Serial.print(F("). Max ")); Serial.print(MAX_ROWS); Serial.println(F(" - lengthen the interval."));
    return;
  }
  if (warm) { if (!askDouble("Stability band = declared resolution from T1 (kg)", 0.05, res)) return; }
  else      { if (!askDouble("Sustained load (kg)", 30, sp)) return; if (sp > setpointCapKg) sp = setpointCapKg; }

  uint8_t dec[6] = { 0, 5, 1, 3, 2, 1 };
  const char* sheet = warm ? "T6_Warmup_Drift" : (loopOn ? "T7_Creep_LoopOn" : "T8_Creep_LoopOff");
  char cell[48]; snprintf(cell, sizeof(cell), "%s!A13", sheet);
  tblBegin(sheet, cell, 6, dec);

  if (!warm) {
    if (!doRetract() || !holdFor(1000)) { testEnd(true); pasteTable(); return; }
    SeekResult s; if (seekAndSettle(sp, s) != 0) { testEnd(true); pasteTable(); return; }
    if (!loopOn) { stopHold(); live("   loop stopped; leadscrew holds position"); }
  } else {
    currentZero();
  }

  // Stability window for the warm-up: last 10 minutes.
  const int WIN = 120;                                  // up to 120 points
  int winN = (int)(600 / every); if (winN > WIN) winN = WIN; if (winN < 2) winN = 2;
  float winF[WIN], winT[WIN]; int winFill = 0, winHead = 0; long stableAtS = -1;

  Tick t; unsigned long t0 = millis(), next = 0, total = (unsigned long)mins * 60000UL;
  double sC = 0, sI = 0, sT = 0, sPwmOn = 0; long nS = 0, nT = 0;
  // Row at t = 0 is the first sample; every later row is the average of
  // the interval that ENDS at its time stamp.
  while (millis() - t0 <= total) {
    if (!tick(t)) break;
    sC += t.counts; sI += fabsf(t.amps); if (!isnan(t.temp)) { sT += t.temp; nT++; }
    sPwmOn += t.pwm != 0 ? 1 : 0; nS++;
    unsigned long el = millis() - t0;
    if (el >= next) {
      double mc = sC / nS;
      float v[6] = { (float)(next / 1000), (float)countsToKg(mc), (float)mc,
                     (float)(sI / nS), nT ? (float)(sT / nT) : NAN, (float)(100.0 * sPwmOn / nS) };
      if (!tblAdd(v)) break;
      live("%s  t=%5lu s  F=%.4f kg  I=%.3f A  Tdrv=%.1f C  rows=%d",
           sheet, next / 1000, v[1], v[3], v[4], tblRows);
      if (warm) {
        winF[winHead] = v[1]; winT[winHead] = v[4]; winHead = (winHead + 1) % winN;
        if (winFill < winN) winFill++;
        if (winFill == winN && stableAtS < 0) {
          float fmn = 1e9, fmx = -1e9, tmn = 1e9, tmx = -1e9;
          for (int k = 0; k < winN; k++) { fmn = fmin(fmn, winF[k]); fmx = fmax(fmx, winF[k]);
                                           tmn = fmin(tmn, winT[k]); tmx = fmax(tmx, winT[k]); }
          if (fmx - fmn <= res && tmx - tmn <= 0.5f) {
            stableAtS = (long)(next / 1000) - (long)(winN - 1) * every;
            if (stableAtS < 0) stableAtS = 0;
            Serial.print(F("\n>> STABLE: force within ")); Serial.print(res, 3);
            Serial.print(F(" kg and temperature within 0.5 C for 10 min.\n>> Suggested warm-up period: "));
            Serial.print(stableAtS / 60.0, 1); Serial.println(F(" min (type it into T6_Warmup_Drift, cell I17).\n"));
            beep(300);
          }
        }
      }
      sC = sI = sT = sPwmOn = 0; nS = nT = 0;
      next += (unsigned long)every * 1000UL;
    }
  }
  if (warm && stableAtS < 0) Serial.println(F("\n>> Not stable within the record - run longer."));
  testEnd(!warm);
  pasteTable();
}

// =====================================================================
//  TEST 9 — STEP RESPONSE  ->  T9_Step_Response!A13
// =====================================================================
void test9_step() {
  rule(); Serial.println(F("TEST 9 - STEP RESPONSE  (sheet T9_Step_Response)")); rule();
  Serial.println(F("Logs every control tick (20 ms) through a commanded step."));
  if (!testReady(true)) return;
  double from, to; long secs;
  if (!askDouble("Start force (kg)", 20, from)) return;
  if (!askDouble("Step to (kg)", 40, to)) return;
  if (!askLong("Log duration after the step (s, max 30)", 20, secs)) return;
  if (secs > 30) secs = 30;
  from = fmin(from, setpointCapKg); to = fmin(to, setpointCapKg);
  uint8_t dec[6] = { 2, 2, 0, 4, 0, 3 };
  tblBegin("T9_Step_Response", "T9_Step_Response!A13", 6, dec);
  if (!doRetract() || !holdFor(1000)) { testEnd(true); pasteTable(); return; }
  SeekResult s; if (seekAndSettle(from, s) != 0) { testEnd(true); pasteTable(); return; }
  live("   holding the start point for 5 s");
  if (!holdFor(5000)) { testEnd(true); pasteTable(); return; }
  targetKg = to; regime = REG_CONTACT; stallTimerRunning = false;
  Tick t; unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)secs * 1000UL) {
    if (!tick(t)) break;
    float v[6] = { (float)from, (float)to, (float)(millis() - t0), (float)t.f, (float)t.pwm, t.amps };
    if (!tblAdd(v)) break;
  }
  live("   logged %d rows", tblRows);
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 10 — CYCLIC AMPLITUDE FIDELITY  ->  T10_Cyclic (first empty row)
// =====================================================================
void test10_cyclic() {
  rule(); Serial.println(F("TEST 10 - CYCLIC AMPLITUDE FIDELITY  (sheet T10_Cyclic)")); rule();
  Serial.println(F("Square-wave cycling. Run once per half-period; paste each run below"));
  Serial.println(F("the last. The sheet builds the transmission ratio T per half-period."));
  if (!testReady(true)) return;
  double lo, hi; long half, cyc;
  if (!askDouble("Lower level (kg)", 20, lo)) return;
  if (!askDouble("Upper level (kg)", 40, hi)) return;
  if (!askLong("Half-period = dwell at each level (s)", 10, half)) return;
  if (!askLong("Number of cycles (first 4 are discarded by the sheet)", 20, cyc)) return;
  hi = fmin(hi, setpointCapKg);
  uint8_t dec[10] = { 0, 2, 2, 0, 4, 4, 0, 0, 2, 1 };
  tblBegin("T10_Cyclic", "T10_Cyclic!A13 (or the first empty row)", 10, dec);
  if (!doRetract() || !holdFor(1000)) { testEnd(true); pasteTable(); return; }
  SeekResult s; if (seekAndSettle(lo, s) != 0) { testEnd(true); pasteTable(); return; }
  Tick t;
  for (long c = 1; c <= cyc && !abortReq && !faultCode; c++) {
    double peak = -1e9, valley = 1e9; float iPk = 0; long tUp = -1, tDn = -1;
    targetKg = hi; regime = REG_CONTACT; stallTimerRunning = false;
    unsigned long t0 = millis();
    while (millis() - t0 < (unsigned long)half * 1000UL) {
      if (!tick(t)) break;
      if (t.f > peak) peak = t.f; iPk = fmax(iPk, fabsf(t.amps));
      if (tUp < 0 && fabs(t.f - hi) <= DEADBAND_KG) tUp = millis() - t0;
    }
    if (abortReq || faultCode) break;
    targetKg = lo; stallTimerRunning = false; t0 = millis();
    while (millis() - t0 < (unsigned long)half * 1000UL) {
      if (!tick(t)) break;
      if (t.f < valley) valley = t.f; iPk = fmax(iPk, fabsf(t.amps));
      if (tDn < 0 && fabs(t.f - lo) <= DEADBAND_KG) tDn = millis() - t0;
    }
    if (abortReq || faultCode) break;
    float v[10] = { (float)c, (float)lo, (float)hi, (float)half, (float)peak, (float)valley,
                    tUp < 0 ? NAN : (float)tUp, tDn < 0 ? NAN : (float)tDn, iPk, t.temp };
    tblAdd(v);
    live("   cycle %ld: peak %.3f  valley %.3f  T=%.3f  Ipk %.2f A  Tdrv %.1f C",
         c, peak, valley, (peak - valley) / (hi - lo), iPk, t.temp);
  }
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 11 — MOTOR CURRENT vs FORCE  ->  T11_Current_vs_Force (first empty row)
//  Open-loop push at a constant PWM from free air into the seated
//  specimen, logging current against force. Gives the running current, the
//  current-per-kg relationship and the near-stall current, so the rig's
//  overcurrent trip and current clamp can be set from data.
// =====================================================================
void test11_current() {
  rule(); Serial.println(F("TEST 11 - MOTOR CURRENT vs FORCE  (sheet T11_Current_vs_Force)")); rule();
  Serial.println(F("OPEN-LOOP push at a constant PWM until the force cap is reached."));
  Serial.println(F("The force can overshoot the cap by a few kg: keep the cap well below"));
  Serial.println(F("what the specimen and fixture can take. Overload and overcurrent"));
  Serial.println(F("protection stay active. Run at 2-3 PWM levels; paste each below."));
  if (!testReady(true)) return;
  long pwm, divisor; double cap;
  if (!askLong("Constant PWM (40-200)", 80, pwm)) return;
  pwm = constrain(pwm, PWM_MIN_MOVE, PWM_MAX);
  if (!askDouble("Force cap (kg)", fmin(50.0, setpointCapKg), cap)) return;
  cap = fmin(cap, setpointCapKg);
  if (!askLong("Log every N ticks (1 = 20 ms)", 2, divisor)) return;
  if (divisor < 1) divisor = 1;
  uint8_t dec[6] = { 0, 0, 4, 3, 1, 3 };
  tblBegin("T11_Current_vs_Force", "T11_Current_vs_Force!A13 (or the first empty row)", 6, dec);
  if (!doRetract() || !holdFor(1500)) { testEnd(true); pasteTable(); return; }
  running = false; enableMotor(true);
  Tick t; unsigned long t0 = millis(); long k = 0;
  while (millis() - t0 < 60000UL) {
    if (eotPush) { live("   extend end of travel - stopped"); break; }
    applyMotor((int)pwm);
    if (!tick(t)) break;
    if (k++ % divisor == 0) {
      float v[6] = { (float)pwm, (float)(millis() - t0), (float)t.f, t.amps, t.temp, iRawA };
      if (!tblAdd(v)) break;
    }
    if (t.f >= cap) { live("   force cap reached (%.2f kg)", t.f); break; }
  }
  motorStop(); enableMotor(false);
  live("   logged %d rows, peak current since boot %.2f A", tblRows, iPeakBootA);
  testEnd(true);
  pasteTable();
}

// =====================================================================
//  TEST 12 — PROTECTIVE FUNCTION VERIFICATION  ->  T12_Safety!A13 (9 rows)
//  Items are provoked deliberately, most at a REDUCED threshold so the
//  function can be shown to work at a safe load. Rows are kept per item;
//  run items in any order, then P prints all nine rows.
// =====================================================================
float safetyRows[9][8];
bool  safetyHave[9] = { false };
void safetyPrinter() {
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 8; c++) {
      if (c) Serial.write('\t');
      if (c == 0) { Serial.print(r + 1); continue; }
      if (!safetyHave[r]) continue;
      float v = safetyRows[r][c];
      static const uint8_t dec[8] = { 0, 0, 0, 0, 0, 3, 3, 2 };
      if (!isnan(v)) Serial.print(v, dec[c]);
    }
    Serial.write('\n');
  }
}
void safetyStore(int item, bool provoked, bool detected, int code, float tStop,
                 float resid, float iAfter, float thr) {
  float v[8] = { (float)item, provoked ? 1.f : 0.f, detected ? 1.f : 0.f, (float)code,
                 tStop, resid, iAfter, thr };
  for (int c = 0; c < 8; c++) safetyRows[item - 1][c] = v[c];
  safetyHave[item - 1] = true;
  Serial.print(F("   recorded item ")); Serial.print(item);
  Serial.print(F(": ")); Serial.print(detected ? F("DETECTED") : F("NOT detected"));
  Serial.print(F(" (")); Serial.print(FAULT_NAMES[code]); Serial.print(F(")  t_stop="));
  Serial.print(tStop, 0); Serial.print(F(" ms  residual=")); Serial.print(resid, 3); Serial.println(F(" kg"));
}
// Wait for the motor current to collapse after a stop; returns ms since tRef.
float timeToCurrentZero(unsigned long tRef) {
  Tick t; unsigned long t0 = millis();
  while (millis() - t0 < 3000) {
    sampleCurrent(); pollSafetyInputs();
    if (fabsf(iRawA) < EOT_CURRENT_A) return (float)(millis() - tRef);
    delay(2);
  }
  (void)t; return NAN;
}
float residualAfter(unsigned long ms) {
  unsigned long t0 = millis(); while (millis() - t0 < ms) { pollSafetyInputs(); readForceKgFiltered(); }
  return (float)readForceKgFiltered();
}
// Restore every threshold and clear the fault the item provoked.
void safetyRestore() {
  ovlTripKg = OVERLOAD_TRIP_KG; ocTripA = OC_TRIP_A_DEFAULT; tempTripC = TEMP_TRIP_C_DEFAULT;
  stopHold();
  if (faultCode) { Serial.println(F("   Clearing the provoked fault...")); clearFault(false); }
}

void safetyItem(int item) {
  abortReq = false;
  if (faultCode) { Serial.println(F("   Clear the existing fault with Z first.")); return; }
  double thr; long tmo;
  switch (item) {
    case 1: {   // OVERLOAD at a reduced trip level
      Serial.println(F("1 OVERLOAD TRIP - the trip level is lowered for this run."));
      if (!askDouble("Reduced trip level (kg)", 15, thr)) return;
      if (!testReady(true)) return;
      doRetract();
      ovlTripKg = thr; startHold(fmin(thr + 5, setpointCapKg));
      Tick t; unsigned long tCross = 0, t0 = millis();
      while (millis() - t0 < 60000UL && !faultCode && !abortReq) { tick(t); if (!tCross && t.f > thr) tCross = millis(); }
      bool det = faultCode == FC_OVERLOAD; int code = faultCode;
      float ts = tCross ? timeToCurrentZero(tCross) : NAN; float res = residualAfter(2000);
      safetyStore(1, true, det, code, ts, res, iRawA, (float)thr);
      ovlTripKg = OVERLOAD_TRIP_KG; safetyRestore(); doRetract(); break;
    }
    case 2: case 3: {   // STALL / APPROACH TIMEOUT
      if (item == 2) {
        Serial.println(F("2 STALL - arrange the load path so force CANNOT rise once in contact"));
        Serial.println(F("  (e.g. a compliant stop that bottoms, or decouple the actuator rod)."));
      } else {
        Serial.println(F("3 APPROACH TIMEOUT - REMOVE the specimen so nothing is contacted."));
        Serial.println(F("  If the actuator reaches its end first, END OF TRAVEL will stop it"));
        Serial.println(F("  instead; that is recorded as the function that acted."));
      }
      if (!askDouble("Commanded hold (kg)", 10, thr)) return;
      if (!testReady(true)) return;
      doRetract();
      startHold(fmin(thr, setpointCapKg));
      Tick t; unsigned long t0 = millis();
      while (millis() - t0 < 60000UL && !faultCode && !abortReq) tick(t);
      int code = faultCode;
      bool det = (item == 2) ? code == FC_STALL : (code == FC_APPROACH || code == FC_EOT);
      float ts = faultCode ? timeToCurrentZero(faultAtMs) : NAN;
      safetyStore(item, true, det, code, ts, residualAfter(1000), iRawA, (float)thr);
      safetyRestore(); doRetract(); break;
    }
    case 4: case 5: {   // END OF TRAVEL
      int dir = item == 4 ? +1 : -1;
      Serial.println(dir > 0 ? F("4 END OF TRAVEL, EXTEND - remove the specimen; drives OUT until the actuator stops itself.")
                             : F("5 END OF TRAVEL, RETRACT - drives IN until the actuator stops itself."));
      if (!askLong("Time limit (s)", 60, tmo)) return;
      if (!testReady(true)) return;
      running = false; enableMotor(true);
      Tick t; unsigned long t0 = millis(), tNoCur = 0;
      while (millis() - t0 < (unsigned long)tmo * 1000UL && !faultCode && !abortReq) {
        if ((dir > 0 && eotPush) || (dir < 0 && eotRetract)) break;
        applyMotor(dir * 120);
        tick(t);
        if (fabsf(iRawA) < EOT_CURRENT_A) { if (!tNoCur) tNoCur = millis(); } else tNoCur = 0;
      }
      bool det = dir > 0 ? eotPush : eotRetract;
      unsigned long tStop = millis();
      motorStop(); enableMotor(false);
      safetyStore(item, true, det, det ? FC_EOT : faultCode, tNoCur ? (float)(tStop - tNoCur) : NAN,
                  (float)readForceKgFiltered(), iRawA, EOT_CURRENT_A);
      safetyRestore();
      if (dir > 0) doRetract(); else doJog(+1);
      break;
    }
    case 6: {   // E-STOP UNDER LOAD
      Serial.println(F("6 E-STOP UNDER LOAD - the rig holds a load; you press the e-stop."));
      if (!askDouble("Load to hold (kg)", 20, thr)) return;
      if (!testReady(true)) return;
      doRetract();
      SeekResult s; if (seekAndSettle(fmin(thr, setpointCapKg), s) != 0) { safetyRestore(); return; }
      Serial.println(F("\n>> PRESS THE E-STOP NOW (within 60 s)."));
      Tick t; unsigned long t0 = millis();
      while (millis() - t0 < 60000UL && !faultCode && !abortReq) tick(t);
      bool det = faultCode == FC_ESTOP;
      float ts = det ? timeToCurrentZero(estopChangedMs) : NAN;
      float res = residualAfter(2000);
      safetyStore(6, true, det, faultCode, ts, res, iRawA, (float)thr);
      Serial.println(F("   Release the e-stop, then ENTER."));
      waitEstop(false); safetyRestore(); doRetract(); break;
    }
    case 7: {   // DRIVER FAULT PIN — monitored, not provoked
      Serial.println(F("7 DRIVER FAULT (G2 FLT) - NOT provoked: it would need a short circuit"));
      Serial.println(F("  or undervoltage. Records that the input is read and currently clear."));
      for (int i = 0; i < 20; i++) { pollSafetyInputs(); delay(5); }
      bool pinOk = digitalRead(MOTOR_FAULT_PIN) == HIGH || estopActive;
      safetyStore(7, false, pinOk, FC_NONE, NAN, NAN, NAN, NAN);
      break;
    }
    case 8: {   // OVERCURRENT at a reduced trip level
      Serial.println(F("8 OVERCURRENT TRIP - the trip level is lowered for this run."));
      Serial.println(F("  OPEN-LOOP push at a constant PWM (as in Test 11), so the current"));
      Serial.println(F("  stays above the trip level for the required 0.5 s. The closed loop"));
      Serial.println(F("  pulses the motor and never holds a current that long."));
      Serial.println(F("  Pick a level just ABOVE the free-running current (see Test 11)."));
      double ocThr, cap; long pwm;
      if (!askDouble("Reduced trip level (A)", 0.5, ocThr)) return;
      if (!askLong("Constant PWM (40-120)", 60, pwm)) return;
      if (!askDouble("Force cap if it does not trip (kg)", 60, cap)) return;
      pwm = constrain(pwm, PWM_MIN_MOVE, 120); cap = fmin(cap, setpointCapKg);
      if (!testReady(true)) return;
      if (!doRetract() || !holdFor(1500)) { safetyRestore(); return; }
      ocTripA = (float)ocThr; running = false; enableMotor(true);
      Tick t; unsigned long t0 = millis(), tOver = 0;
      while (millis() - t0 < 60000UL && !faultCode && !abortReq) {
        if (eotPush) break;
        applyMotor((int)pwm);
        if (!tick(t)) break;
        if (!tOver && fabsf(t.amps) > ocTripA) tOver = millis();
        if (t.f >= cap) { live("   force cap reached (%.2f kg) without a trip", t.f); break; }
      }
      motorStop(); enableMotor(false); commandedSignedPWM = 0;
      bool det = faultCode == FC_OVERCURRENT; int code = faultCode;
      float ts = tOver ? timeToCurrentZero(tOver) : NAN;
      safetyStore(8, true, det, code, ts, residualAfter(2000), iRawA, (float)ocThr);
      safetyRestore(); doRetract(); break;
    }
    case 9: {   // OVER-TEMPERATURE at a reduced trip level
      sampleTemperature();
      Serial.print(F("9 OVER-TEMPERATURE - driver now ")); Serial.print(tempC, 1); Serial.println(F(" C."));
      Serial.println(F("  The trip is set a few degrees above that; warm the thermistor with"));
      Serial.println(F("  a fingertip or warm air until it trips. No motion is needed."));
      if (!askDouble("Reduced trip level (C)", tempC + 3.0, thr)) return;
      abortReq = false;
      tempTripC = (float)thr;
      unsigned long t0 = millis(), tCross = 0; Tick t;
      while (millis() - t0 < 120000UL && !faultCode && !abortReq) { tick(t); if (!tCross && tempC >= thr) tCross = millis(); }
      bool det = faultCode == FC_OVERTEMP;
      long dT = (long)faultAtMs - (long)tCross;        // the trip can precede the logged crossing
      safetyStore(9, true, det, faultCode, (tCross && det) ? (float)(dT > 0 ? dT : 0) : NAN,
                  NAN, NAN, (float)thr);
      tempTripC = TEMP_TRIP_C_DEFAULT;
      Serial.println(F("   Let it cool below 70 C; then Z clears it."));
      safetyRestore(); break;
    }
  }
}
void test12_safety() {
  while (true) {
    rule(); Serial.println(F("TEST 12 - PROTECTIVE FUNCTIONS  (sheet T12_Safety)")); rule();
    Serial.println(F("Run items in any order; each is kept. Hand on the e-stop throughout."));
    const char* names[9] = { "Overload trip (reduced threshold)", "Stall detection",
      "Approach timeout", "End of travel - extend", "End of travel - retract",
      "E-stop under load", "Driver fault pin (monitored)", "Overcurrent trip (reduced threshold)",
      "Over-temperature trip (reduced threshold)" };
    for (int i = 0; i < 9; i++) {
      Serial.print(F("   ")); Serial.print(i + 1); Serial.print(F("  ")); Serial.print(names[i]);
      Serial.println(safetyHave[i] ? F("   [done]") : F(""));
    }
    Serial.println(F("  10/11  Link loss and watchdog: run with the OPERATIONAL firmware and HMI;"));
    Serial.println(F("         type those results straight into T12 rows 22-23."));
    Serial.println(F("   P  print the 9 rows for T12_Safety!A13      Q  back to the main menu"));
    Serial.print(F("Item: "));
    readLineBlocking(); trimLine(); Serial.println(lineBuf);
    char c = toupper(lineBuf[0]);
    if (c == 'Q' || c == 'X') return;
    if (c == 'P') { pasteProtocol("T12_Safety", "T12_Safety!A13  (9 rows)", 9, safetyPrinter); continue; }
    int it = atoi(lineBuf);
    if (it >= 1 && it <= 9) safetyItem(it);
  }
}

// =====================================================================
//  UTILITIES
// =====================================================================
void util_live() {
  Serial.println(F("\nLive readout. ENTER stops."));
  abortReq = false;
  while (true) {
    long c = readRawCounts(); sampleCurrent(); sampleTemperature(); pollSafetyInputs(); updateLeds();
    const char* eot = eotPush && eotRetract ? "both" : eotPush ? "extend" : eotRetract ? "retract" : "none";
    live("counts %8ld  F %8.3f kg  I %6.2f A (zero %4.0f mV)  Tdrv %5.1f C  estop %s  FLT %s  EOT %s  fault %s%s",
         c, countsToKg((double)c), iAmps, iZeroMv, tempOk ? tempC : NAN,
         estopActive ? "PRESSED" : "released", driverFaultLive ? "FAULT" : "ok",
         eot, FAULT_NAMES[faultCode], isSaturated(c) ? "  !! ADC SATURATED" : "");
    unsigned long t0 = millis();
    while (millis() - t0 < 300) { if (readLineNB()) return; pollSafetyInputs(); delay(5); }
  }
}
// Retract while a fault stays latched, so a rig left pressing (e.g. after an
// OVERLOAD) can always be unloaded. Retract is the safe direction. Refused
// with the E-stop pressed or the driver reporting FLT. The overload trip is
// suspended for the move (retracting can only lower the force); the
// "force rose while retracting" guard and every other protection stay on.
void faultRetract(double mm) {
  if (estopActive) { Serial.println(F("   e-stop pressed - release it first")); return; }
  if (digitalRead(MOTOR_FAULT_PIN) == LOW) { Serial.println(F("   driver FLT low - cannot drive")); return; }
  int saved = faultCode; faultCode = FC_NONE; abortReq = false;
  Serial.print(F("   recovery retract, ")); Serial.print(FAULT_NAMES[saved]); Serial.println(F(" stays latched"));
  retractRecovering = true;
  openLoopMove(-RETRACT_PWM, mm, RETRACT_SPEED_MM_S, "retracting (fault recovery)");
  retractRecovering = false;
  int fresh = faultCode;
  faultCode = saved;
  if (fresh) { Serial.print(F("   (stopped by: ")); Serial.print(FAULT_NAMES[fresh]); Serial.println(F(")")); }
}
void util_manual() {
  Serial.println(F("\nManual jog: + extend 1 mm, - retract 1 mm, b back off 5 mm, q quit."));
  if (faultCode) Serial.println(F("   Fault latched: only - and b (retract) work. q, then Z at the menu to clear."));
  while (true) {
    readLineBlocking(); trimLine(); char c = lineBuf[0];
    if (faultCode && (c == '-' || c == 'b' || c == 'B')) {
      faultRetract(c == '-' ? JOG_DIST_MM : RETRACT_DIST_MM);
      live("   F = %.3f kg", readForceKgFiltered()); continue;
    }
    if (faultCode && c != 'q' && c != 'Q') { Serial.println(F("   fault latched - only - / b retract here; q, then Z at the menu")); continue; }
    if (estopActive && c != 'q' && c != 'Q') { Serial.println(F("   e-stop pressed")); continue; }
    abortReq = false;
    if (c == '+') doJog(+1); else if (c == '-') doJog(-1);
    else if (c == 'b' || c == 'B') doRetract();
    else if (c == 'q' || c == 'Q') return;
    live("   F = %.3f kg", readForceKgFiltered());
  }
}
// G -- PI gains for THIS SESSION ONLY (tuning with Test 9). Put the final
// values into the sketch AND the rig firmware; Test 0 records what is in use.
void util_gains() {
  Serial.println(F("\nPI gains in use (this session):"));
  Serial.print(F("  Kp = ")); Serial.print(Kp, 2); Serial.println(F("  PWM per kg"));
  Serial.print(F("  Ki = ")); Serial.print(Ki, 2); Serial.println(F("  PWM per kg*s"));
  Serial.println(F("Tuning: Ki = 0, raise Kp until a Test 9 step overshoots slightly, back off ~30 %;"));
  Serial.println(F("then raise Ki until the last bit of error goes within a few seconds, no overshoot."));
  Serial.println(F("Check a low step (10->20 kg) AND a high one (60->80 kg)."));
  long ch; if (!askLong("Change them now? 1=yes 0=no", 0, ch) || ch != 1) return;
  double v;
  if (askDouble("New Kp", Kp, v)) Kp = constrain(v, 0.0, 50.0);
  if (askDouble("New Ki", Ki, v)) Ki = constrain(v, 0.0, 50.0);
  Serial.print(F("   Now Kp = ")); Serial.print(Kp, 2); Serial.print(F(", Ki = ")); Serial.println(Ki, 2);
  Serial.println(F("   Edit Kp / Ki in BOTH sketches to keep them."));
}
void util_constants() {
  Serial.println(F("\nCalibration constants in use:"));
  Serial.print(F("  slope     = ")); Serial.println(CAL_SLOPE_KG_PER_COUNT, 12);
  Serial.print(F("  intercept = ")); Serial.println(CAL_INTERCEPT_KG, 9);
  Serial.println(F("New values apply to THIS SESSION ONLY - edit the sketch to keep them."));
  long ch; if (!askLong("Change them now? 1=yes 0=no", 0, ch) || ch != 1) return;
  askDouble("New slope (kg per count)", CAL_SLOPE_KG_PER_COUNT, CAL_SLOPE_KG_PER_COUNT);
  askDouble("New intercept (kg)", CAL_INTERCEPT_KG, CAL_INTERCEPT_KG);
  forceSeeded = false;
}
void util_cap() {
  Serial.print(F("\nSetpoint cap now ")); Serial.print(setpointCapKg, 1); Serial.println(F(" kg."));
  double v; if (!askDouble("New cap (kg, max 90)", setpointCapKg, v)) return;
  setpointCapKg = constrain(v, 5.0, 90.0);
}
void printOrder() {
  rule(); Serial.println(F("RECOMMENDED ORDER OF WORK")); rule();
  Serial.println(F(" BEFORE THE SESSION"));
  Serial.println(F("   a. UTM load-cell calibration with THIS board (LoadCellCalibrationFirmware),"));
  Serial.println(F("      into Load_Cell_Calibration_Data_V1.xlsx."));
  Serial.println(F("   b. Carry its numbers into LC_Reference (orange cells)."));
  Serial.println(F("   c. Flash those constants into this sketch AND the rig firmware."));
  Serial.println(F("   d. Leave the rig unpowered for 2 h or more (cold start). Fill in Setup."));
  Serial.println(F(" SESSION"));
  Serial.println(F("   1.  Test 0   Session record          right after power-on, still cold"));
  Serial.println(F("   2.  Test 6   Warm-up and zero drift  60 min, unloaded, untouched"));
  Serial.println(F("   3.  Test 1   Resolution and noise    sets the resolution all others use"));
  Serial.println(F("   4.  P        Preload 3x              ISO 7500-1 Cl. 6.4.3"));
  Serial.println(F("   4b. G + 9    Tune the PI             low step 10->20, high 60->80 kg;"));
  Serial.println(F("                                        flash the final Kp/Ki, rerun Test 0"));
  Serial.println(F("   5.  Test 2   Commanded vs achieved"));
  Serial.println(F("   6.  Test 3   Repeatability           once per setpoint"));
  Serial.println(F("   7.  Test 5   Direction dependence"));
  Serial.println(F("   8.  Test 4   Seating / Gauge R&R     once per operator"));
  Serial.println(F("   9.  Test 9   Step response"));
  Serial.println(F("  10.  Test 10  Cyclic fidelity         once per half-period"));
  Serial.println(F("  11.  Test 11  Current vs force        2-3 PWM levels"));
  Serial.println(F("  12.  Test 7   Creep, loop on          35 min"));
  Serial.println(F("  13.  Test 8   Creep, loop off         35 min"));
  Serial.println(F("  14.  Test 12  Protective functions    LAST: it trips faults on purpose"));
  Serial.println(F(" AFTER"));
  Serial.println(F("   Ambient temperature at end on Setup; T13_Uncertainty and Summary fill"));
  Serial.println(F("   themselves. Copy Summary into the paper."));
  rule();
}
void printMenu() {
  Serial.println();
  rule();
  Serial.println(F("  FORCE RIG - CALIBRATION & CHARACTERISATION HARNESS  " FW_VERSION));
  rule();
  Serial.println(F("  Number = workbook sheet. Test 6 fills sheet T6_..., and so on."));
  Serial.println(F("    0  Session record                      -> Setup"));
  Serial.println(F("    1  Resolution and noise floor          -> T1_Noise_Resolution"));
  Serial.println(F("    2  Commanded vs achieved force         -> T2_Command_Accuracy"));
  Serial.println(F("    3  Short-term repeatability            -> T3_Repeatability"));
  Serial.println(F("    4  Seating reproducibility / Gauge R&R -> T4_Seating_GageRR"));
  Serial.println(F("    5  Direction dependence                -> T5_Direction"));
  Serial.println(F("    6  Warm-up and zero drift (unloaded)   -> T6_Warmup_Drift"));
  Serial.println(F("    7  Creep, control loop ON              -> T7_Creep_LoopOn"));
  Serial.println(F("    8  Creep, control loop OFF             -> T8_Creep_LoopOff"));
  Serial.println(F("    9  Step response                       -> T9_Step_Response"));
  Serial.println(F("   10  Cyclic amplitude fidelity           -> T10_Cyclic"));
  Serial.println(F("   11  Motor current vs force              -> T11_Current_vs_Force"));
  Serial.println(F("   12  Protective functions                -> T12_Safety"));
  Serial.println(F("        (T13_Uncertainty and Summary are calculated, no test)"));
  Serial.println();
  Serial.println(F("    O  Recommended order      P  Preload 3x       L  Live readout"));
  Serial.println(F("    M  Manual jog             K  Constants        C  Setpoint cap"));
  Serial.println(F("    G  PI gains (session)"));
  Serial.println(F("    R  Reprint last results   Z  Clear fault      ?  This menu"));
  Serial.println(F("    X  (during a test) abort"));
  rule();
  Serial.print(F("  Setpoint cap ")); Serial.print(setpointCapKg, 1);
  Serial.print(F(" kg   Driver ")); if (tempOk) Serial.print(tempC, 1); else Serial.print(F("--"));
  Serial.print(F(" C   E-stop ")); Serial.print(estopActive ? F("PRESSED") : F("released"));
  Serial.print(F("   Fault ")); Serial.println(FAULT_NAMES[faultCode]);
  Serial.print(F("Select: "));
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  pinMode(MOTOR_DIR_PIN, OUTPUT); digitalWrite(MOTOR_DIR_PIN, LOW);
  pwmBegin(); pwmWrite(0);
  pinMode(BUZZER_PIN, OUTPUT);    digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED_RED_PIN, OUTPUT);   digitalWrite(LED_RED_PIN, HIGH);
  pinMode(LED_GREEN_PIN, OUTPUT); digitalWrite(LED_GREEN_PIN, LOW);
  pinMode(EMER_SENSE_PIN, INPUT_PULLUP);
  pinMode(MOTOR_FAULT_PIN, INPUT_PULLUP);
  analogReadResolution(12); analogSetAttenuation(ADC_11db);

  Serial.begin(115200);
  unsigned long ts = millis(); while (!Serial && millis() - ts < 3000) {}

  estopActive = estopRawLast = digitalRead(EMER_SENSE_PIN) == HIGH;
  estopChangedMs = millis();

  pinMode(ADS1220_CS_PIN, OUTPUT); digitalWrite(ADS1220_CS_PIN, HIGH);
  pinMode(ADS1220_DRDY_PIN, INPUT);
  SPI.begin();

  Serial.println(F("\n\nCalibration harness " FW_VERSION " starting..."));
  Serial.print(F("Motor PWM:      ")); Serial.print(MOTOR_PWM_FREQ_HZ); Serial.print(F(" Hz, "));
  Serial.print(MOTOR_PWM_BITS); Serial.println(pwmOk ? F("-bit, OK") : F("-bit, SETUP FAILED - the motor cannot be driven"));
  bool ok = adsInit();
  Serial.print(F("ADS1220:        ")); Serial.println(ok ? F("register readback OK") : F("NO RESPONSE - check SPI/power"));
  Serial.print(F("Reference:      ")); Serial.println(ADS_RATIOMETRIC ? F("ratiometric (matches rig firmware)") : F("internal 2.048 V"));
  currentZero();
  for (int i = 0; i < 20; i++) sampleTemperature();
  long c = readRawCounts(); readForceKgFiltered();
  Serial.print(F("Resting force:  ")); Serial.print(countsToKg((double)c), 3);
  Serial.print(F(" kg (")); Serial.print(c); Serial.println(F(" counts)"));
  if (isSaturated(c)) Serial.println(F("!! ADC SATURATED - check the load cell wiring and reference."));
  Serial.print(F("Current zero:   ")); Serial.print(iZeroMv, 0); Serial.println(iSensorOk ? F(" mV OK") : F(" mV OUT OF RANGE"));
  Serial.print(F("Driver temp:    ")); if (tempOk) { Serial.print(tempC, 1); Serial.println(F(" C")); } else Serial.println(F("SENSOR FAULT"));
  Serial.print(F("E-stop:         ")); Serial.println(estopActive ? F("PRESSED") : F("released"));
  Serial.println(F("Calibration constants are PROVISIONAL until copied from LC_Reference."));
  Serial.println(F("\nSAFETY: this sketch drives the actuator. Hand on the E-stop."));
  beep(120);
  printOrder();
  printMenu();
}

void loop() {
  idleService();
  if (!readLineNB()) { delay(2); return; }
  trimLine();
  if (!lineBuf[0]) { printMenu(); return; }
  Serial.println(lineBuf);
  char c = toupper(lineBuf[0]);
  bool numeric = isdigit((unsigned char)lineBuf[0]);
  int n = numeric ? atoi(lineBuf) : -1;

  if (numeric) {
    switch (n) {
      case 0:  test0_session(); break;
      case 1:  test1_noise(); break;
      case 2:  test2_command(); break;
      case 3:  test3_repeat(); break;
      case 4:  test4_grr(); break;
      case 5:  test5_direction(); break;
      case 6:  longRecord(6); break;
      case 7:  longRecord(7); break;
      case 8:  longRecord(8); break;
      case 9:  test9_step(); break;
      case 10: test10_cyclic(); break;
      case 11: test11_current(); break;
      case 12: test12_safety(); break;
      default: Serial.println(F("Unknown test number."));
    }
  }
  else if (c == 'O') printOrder();
  else if (c == 'P') { doPreload(true); testEnd(false); }
  else if (c == 'L') util_live();
  else if (c == 'M') util_manual();
  else if (c == 'K') util_constants();
  else if (c == 'G') util_gains();
  else if (c == 'C') util_cap();
  else if (c == 'R') { if (lastPrinter) pasteProtocol(lastSheet, lastCell, lastRows, lastPrinter);
                       else Serial.println(F("Nothing to reprint yet.")); }
  else if (c == 'Z') { if (clearFault(true) && !estopActive) doRetract(); }
  else if (c == '?') { printMenu(); return; }
  else Serial.println(F("Unknown selection."));

  stopHold();             // always leave the machine safe between tests
  abortReq = false;
  printMenu();
}
