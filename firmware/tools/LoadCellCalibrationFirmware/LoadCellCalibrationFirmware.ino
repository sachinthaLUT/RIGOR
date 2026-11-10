#include <SPI.h>
#include <limits.h>

// =====================================================================
//  LOAD CELL CALIBRATION UTILITY  --  Force Rig PCB rev 2
//
//  CHANGES FOR PCB REV 2 (behaviour of every function is unchanged):
//    - Buzzer moved A3 -> D4. A3 is now the driver thermistor; driving it
//      as an output would fight the NTC divider.
//    - ADS1220 reference now RATIOMETRIC (REFP0/REFN0 = bridge excitation),
//      the same as Force_Control_Rig_V3 rev 5. Counts captured here are
//      only valid for the rig if both use the SAME reference setting.
//    - Pins use the Nano ESP32 D-labels, so they are right whatever
//      Tools > Pin Numbering is set to.
//    - G2 motor driver PWM/DIR held LOW, so the actuator can never move
//      while this utility runs, even if it is plugged in.
//    - Boot check: ADS1220 register readback (reports a dead SPI link).
//    - Captures and TEST mode flag a SATURATED ADC (+/- full scale), which
//      means a wiring/reference fault, not a load.
// =====================================================================

// ====================== USER PIN CONFIGURATION ======================
// ADS1220 connections to Arduino Nano ESP32 (SPI), per Force_Rig_PCB rev 2.
// SCK / MOSI / MISO = D13 / D11 / D12, the board's default hardware SPI.
#define ADS1220_CS_PIN     D10   // net CS_ADS1220
#define ADS1220_DRDY_PIN   D9    // net DRDY

// Buzzer
#define BUZZER_PIN         D4    // net Buzzer+  (was A3 on the old board)

// Pololu G2 motor driver -- held OFF by this utility
#define MOTOR_PWM_PIN      D6
#define MOTOR_DIR_PIN      D5

// ADS1220 reference -- MUST MATCH ADS_RATIOMETRIC in Force_Control_Rig_V3.
//   1 = ratiometric: VREF = REFP0/REFN0 (bridge excitation, PCB rev 2)
//   0 = internal 2.048 V reference (old setting)
#define ADS_RATIOMETRIC    1
// ====================================================================

// ====================== CAPTURE SETTINGS ============================
const unsigned long AVG_WINDOW_MS   = 5000;  // how long to average after M / R
const unsigned long DISCARD_LEAD_MS = 750;   // ignore this much at the start of the
                                             // window (load just freshly applied)
const int    SAMPLE_DELAY_MS  = 50;          // delay between samples (~15 samples/s
                                             // after the DRDY wait). The ADC runs at
                                             // 40 SPS (reg1 = 0x14, turbo 20 SPS),
                                             // so every sample is a fresh conversion.
const int    TEST_PRINT_MS    = 100;         // how often to print live data in TEST mode
const long   NOISE_WARN_PP    = 300;         // if window peak-to-peak exceeds this, warn
                                             // RE-MEASURE in TEST mode: ratiometric
                                             // counts are ~2.3x larger per volt than
                                             // with the old 2.048 V reference
const int    MAX_AVG_SAMPLES  = 400;         // safety cap on samples stored per capture
// ====================================================================

// ADS1220 register/command definitions
#define ADS1220_CMD_RESET    0x06
#define ADS1220_CMD_START    0x08
#define ADS1220_CMD_RDATA    0x10
#define ADS1220_CMD_WREG     0x40
#define ADS1220_CMD_RREG     0x20

// Register values, shared by the writes and the boot readback check.
// Identical to Force_Control_Rig_V3 rev 5, so counts here mean the same
// thing there.
#define ADS1220_REG0_VAL   0x0E   // AINp=AIN0 (SIG+), AINn=AIN1 (SIG-), gain 128, PGA on
#define ADS1220_REG1_VAL   0x14   // 20 SPS turbo (= 40 SPS), continuous conversion
#if ADS_RATIOMETRIC
#define ADS1220_REG2_VAL   0x50   // VREF = REFP0/REFN0 + 50/60 Hz rejection
#else
#define ADS1220_REG2_VAL   0x10   // VREF = internal 2.048 V + 50/60 Hz rejection
#endif
#define ADS1220_REG3_VAL   0x00   // defaults

// 24-bit full scale. A reading AT either limit means the ADC is clipped.
const long ADC_FS_POS =  8388607L;
const long ADC_FS_NEG = -8388608L;

SPISettings adsSPI(1000000, MSBFIRST, SPI_MODE1);  // ADS1220 = SPI mode 1

// ====================== MODE STATE MACHINE ==========================
enum Mode { MODE_IDLE, MODE_TEST, MODE_CALIBRATE };
Mode currentMode = MODE_IDLE;
// ====================================================================

// ====================== CAPTURE RESULT TYPE =========================
// Declared up here so the IDE's auto-generated prototype for
// captureAverage() can see the type.
struct CaptureResult {
  double mean;
  long   minV;
  long   maxV;
  long   peakToPeak;
  double stdev;
  int    nSamples;
};
// ====================================================================

void ads1220WriteReg(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_WREG | (reg << 2) | 0x00); // write 1 reg starting at 'reg'
  SPI.transfer(value);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
}

uint8_t ads1220ReadReg(uint8_t reg) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RREG | (reg << 2));  // read 1 reg starting at 'reg'
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
  return v;
}

void ads1220SendCmd(uint8_t cmd) {
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(cmd);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();
}

long ads1220ReadData() {
  uint8_t b0, b1, b2;
  SPI.beginTransaction(adsSPI);
  digitalWrite(ADS1220_CS_PIN, LOW);
  SPI.transfer(ADS1220_CMD_RDATA);
  b0 = SPI.transfer(0x00);  // MSB
  b1 = SPI.transfer(0x00);
  b2 = SPI.transfer(0x00);  // LSB
  digitalWrite(ADS1220_CS_PIN, HIGH);
  SPI.endTransaction();

  // Combine into 24-bit signed value
  long raw = ((long)b0 << 16) | ((long)b1 << 8) | b2;
  if (raw & 0x800000) raw |= 0xFF000000;  // sign extend
  return raw;
}

// Returns true if the registers read back as written (SPI link working).
bool ads1220Init() {
  ads1220SendCmd(ADS1220_CMD_RESET);
  delay(100);

  // ---- Register configuration (see the #defines above) ----
  ads1220WriteReg(0x00, ADS1220_REG0_VAL);
  ads1220WriteReg(0x01, ADS1220_REG1_VAL);
  ads1220WriteReg(0x02, ADS1220_REG2_VAL);
  ads1220WriteReg(0x03, ADS1220_REG3_VAL);

  bool ok = (ads1220ReadReg(0x00) == ADS1220_REG0_VAL) &&
            (ads1220ReadReg(0x01) == ADS1220_REG1_VAL) &&
            (ads1220ReadReg(0x02) == ADS1220_REG2_VAL) &&
            (ads1220ReadReg(0x03) == ADS1220_REG3_VAL);

  ads1220SendCmd(ADS1220_CMD_START);  // start continuous conversions
  delay(100);
  return ok;
}

bool isSaturated(long v) { return v >= ADC_FS_POS || v <= ADC_FS_NEG; }

bool dataReady() {
  // DRDY goes LOW when new data is available
  return digitalRead(ADS1220_DRDY_PIN) == LOW;
}

long readSample() {
  unsigned long t0 = millis();
  while (!dataReady()) {
    if (millis() - t0 > 500) break;  // timeout safeguard
  }
  return ads1220ReadData();
}

void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}

void doubleBeep() {
  beep();
  delay(120);
  beep();
}

// ====================================================================
// captureAverage()
//   After you confirm the UTM is at setpoint and press M (or R), this
//   averages the incoming stream over AVG_WINDOW_MS, discarding the
//   first DISCARD_LEAD_MS. Returns mean plus noise stats so you can
//   judge whether the capture was clean.
// ====================================================================
CaptureResult captureAverage() {
  // Discard the leading portion (freshly applied load may still micro-settle)
  unsigned long discardStart = millis();
  while (millis() - discardStart < DISCARD_LEAD_MS) {
    readSample();
    delay(SAMPLE_DELAY_MS);
  }

  // Collect the averaging window
  static long samples[MAX_AVG_SAMPLES];
  int n = 0;
  long minV = LONG_MAX;
  long maxV = LONG_MIN;
  double sum = 0.0;

  unsigned long winStart = millis();
  while (millis() - winStart < AVG_WINDOW_MS && n < MAX_AVG_SAMPLES) {
    long s = readSample();
    samples[n] = s;
    sum += (double)s;
    if (s < minV) minV = s;
    if (s > maxV) maxV = s;
    n++;
    delay(SAMPLE_DELAY_MS);
  }

  CaptureResult r;
  r.nSamples = n;
  if (n == 0) {  // safety: should never happen
    r.mean = 0; r.minV = 0; r.maxV = 0; r.peakToPeak = 0; r.stdev = 0;
    return r;
  }

  r.mean = sum / n;
  r.minV = minV;
  r.maxV = maxV;
  r.peakToPeak = maxV - minV;

  // Standard deviation
  double sq = 0.0;
  for (int i = 0; i < n; i++) {
    double d = (double)samples[i] - r.mean;
    sq += d * d;
  }
  r.stdev = (n > 1) ? sqrt(sq / (n - 1)) : 0.0;
  return r;
}

// ====================== MENU / MODE HANDLING ========================
void printMenu() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("  Load Cell Utility - Select Mode");
  Serial.println("====================================");
  Serial.println("  T = TEST mode (live data + noise meter)");
  Serial.println("  C = CALIBRATE mode (average on command)");
  Serial.println("  X = return to this menu");
  Serial.println("====================================");
  Serial.print("Enter choice: ");
}

void printCalibrateHelp() {
  Serial.println();
  Serial.println(">> CALIBRATE MODE");
  Serial.print("   Averages ");
  Serial.print(AVG_WINDOW_MS / 1000.0, 1);
  Serial.println(" s after you press a key.");
  Serial.println("   M = capture LOADED reading  (press once UTM is AT setpoint)");
  Serial.println("   R = capture RESTING reading (press once UTM has lifted)");
  Serial.println("   X = return to menu");
}

// TEST-mode noise tracking
unsigned long lastTestPrint = 0;
long ppMin = LONG_MAX;
long ppMax = LONG_MIN;
unsigned long ppWindowStart = 0;
const unsigned long PP_WINDOW_MS = 5000;  // reset noise stats every 5 s

void resetNoiseStats() {
  ppMin = LONG_MAX;
  ppMax = LONG_MIN;
  ppWindowStart = millis();
}

// Handles mode-switching keys (T / C / X). Returns true if handled.
bool handleModeKeys(char c) {
  if (c == 'T' || c == 't') {
    currentMode = MODE_TEST;
    resetNoiseStats();
    Serial.println();
    Serial.println(">> TEST MODE: live data + noise meter. Send 'X' for menu.");
    return true;
  }
  else if (c == 'C' || c == 'c') {
    currentMode = MODE_CALIBRATE;
    printCalibrateHelp();
    return true;
  }
  else if (c == 'X' || c == 'x') {
    currentMode = MODE_IDLE;
    printMenu();
    return true;
  }
  return false;
}
// ====================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }

  // Motor driver OFF: PWM low = G2 brakes, so the actuator cannot move.
  pinMode(MOTOR_PWM_PIN, OUTPUT);
  digitalWrite(MOTOR_PWM_PIN, LOW);
  pinMode(MOTOR_DIR_PIN, OUTPUT);
  digitalWrite(MOTOR_DIR_PIN, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(ADS1220_CS_PIN, OUTPUT);
  digitalWrite(ADS1220_CS_PIN, HIGH);
  pinMode(ADS1220_DRDY_PIN, INPUT);

  SPI.begin();  // default hardware SPI: D13 SCK, D11 MOSI, D12 MISO

  bool adcOk = ads1220Init();

  Serial.println("ADS1220 load cell utility ready.");
  Serial.print("   Reference: ");
  Serial.println(ADS_RATIOMETRIC ? "RATIOMETRIC (REFP0/REFN0) - matches rig rev 5"
                                 : "internal 2.048 V");
  if (!adcOk) {
    Serial.println("   !! ADS1220 register readback FAILED - check SPI wiring, CS, power.");
    doubleBeep();
  }
  printMenu();
}

// ---------------------- TEST MODE LOOP BODY ------------------------
// Live counts + rolling min/max/peak-to-peak so you can measure your
// noise band and set NOISE_WARN_PP / pick averaging settings.
void runTestMode() {
  long sample = readSample();

  if (sample < ppMin) ppMin = sample;
  if (sample > ppMax) ppMax = sample;

  if (millis() - lastTestPrint >= TEST_PRINT_MS) {
    lastTestPrint = millis();
    long pp = ppMax - ppMin;
    Serial.print("Counts: ");
    Serial.print(sample);
    Serial.print("   | win min: ");
    Serial.print(ppMin);
    Serial.print("  max: ");
    Serial.print(ppMax);
    Serial.print("  p2p: ");
    Serial.print(pp);
    if (isSaturated(sample)) Serial.print("   !! ADC SATURATED (wiring/reference fault)");
    Serial.println();
  }

  // Periodically reset so an old spike doesn't dominate forever
  if (millis() - ppWindowStart >= PP_WINDOW_MS) {
    resetNoiseStats();
  }
}

// ------------------- CALIBRATE MODE LOOP BODY ----------------------
// Event-driven: waits for M (loaded) or R (resting), then averages.
void runCalibrateMode() {
  if (Serial.available() <= 0) return;

  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ') return;

  if (c == 'X' || c == 'x') { handleModeKeys(c); return; }

  bool loaded;
  if (c == 'M' || c == 'm') {
    loaded = true;
  } else if (c == 'R' || c == 'r') {
    loaded = false;
  } else {
    Serial.println("   (Unknown key. Use M, R, or X.)");
    return;
  }

  Serial.print(loaded ? "Averaging LOADED reading for "
                      : "Averaging RESTING reading for ");
  Serial.print(AVG_WINDOW_MS / 1000.0, 1);
  Serial.println(" s ... keep load steady.");

  CaptureResult r = captureAverage();

  // ---- The single value to record in Excel ----
  Serial.print(loaded ? ">> APPLIED  (avg counts): "
                      : ">> REMOVED  (avg counts): ");
  Serial.println((long)round(r.mean));

  // Quality report
  Serial.print("   [n=");
  Serial.print(r.nSamples);
  Serial.print("  min=");
  Serial.print(r.minV);
  Serial.print("  max=");
  Serial.print(r.maxV);
  Serial.print("  p2p=");
  Serial.print(r.peakToPeak);
  Serial.print("  stdev=");
  Serial.print(r.stdev, 1);
  Serial.println("]");

  if (isSaturated(r.minV) || isSaturated(r.maxV)) {
    Serial.println("   !! ADC SATURATED during capture - value is INVALID.");
    Serial.println("      Check the load cell wiring and the REFP/REFN reference.");
  }
  if (r.peakToPeak > NOISE_WARN_PP) {
    Serial.println("   !! NOISY capture (p2p high) - consider re-capturing.");
  }

  if (loaded) beep();        // single beep  = loaded captured
  else        doubleBeep();  // double beep = resting captured
}

void loop() {
  switch (currentMode) {
    case MODE_TEST:
      if (Serial.available() > 0) {
        char c = Serial.read();
        if (!(c == '\n' || c == '\r' || c == ' ')) handleModeKeys(c);
      }
      if (currentMode == MODE_TEST) runTestMode();
      break;

    case MODE_CALIBRATE:
      runCalibrateMode();
      break;

    case MODE_IDLE:
    default:
      if (Serial.available() > 0) {
        char c = Serial.read();
        if (!(c == '\n' || c == '\r' || c == ' ')) handleModeKeys(c);
      }
      break;
  }
}
