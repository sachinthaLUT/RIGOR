#include "datalog.h"
#include "state.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ===========================================================================
//  SD WIRING
// ===========================================================================
static const int SD_CS_PIN   = 10;
static const int SD_MOSI_PIN = 11;
static const int SD_SCK_PIN  = 12;
static const int SD_MISO_PIN = 13;

static SPIClass s_sdSPI(HSPI);

static const char* LOG_DIR = "/logs";

static bool     s_mounted   = false;
static bool     s_recording = false;
static File     s_file;
static uint32_t s_count     = 0;
static char     s_name[TESTNAME_MAX + 8] = "";

// ---------------------------------------------------------------------------
//  WRITE BUFFER  -- samples accumulate here and flush in chunks, because a
//  single SD write can block for milliseconds.
// ---------------------------------------------------------------------------
static char   s_buf[1024];
static size_t s_buflen = 0;

// ---------------------------------------------------------------------------
//  RAM FALLBACK (no card). Ring buffer: keeps the most RECENT samples.
// ---------------------------------------------------------------------------
struct RamSample { uint32_t t_ms; float kg; float amps; float temp_c; };
static RamSample* s_ram      = nullptr;
static uint32_t   s_ram_head = 0;
static bool       s_ram_wrap = false;

// ===========================================================================
//  MOUNT
// ===========================================================================
void datalog_begin() {
    s_sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    s_mounted = SD.begin(SD_CS_PIN, s_sdSPI, 20000000);

    if (s_mounted) {
        if (!SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
        Serial.printf("[datalog] SD mounted, %lu MB total, %lu MB free\n",
                      (unsigned long)datalog_total_mb(),
                      (unsigned long)datalog_free_mb());
    } else {
        Serial.println("[datalog] No SD card (or unreadable).");
        Serial.println("[datalog] Cards >= 32 GB must be reformatted as FAT32.");
        Serial.println("[datalog] Falling back to a RAM ring buffer.");
        s_ram = (RamSample*)malloc(sizeof(RamSample) * RAM_FALLBACK_SAMPLES);
        if (!s_ram) Serial.println("[datalog] RAM buffer alloc FAILED - no logging.");
    }
}

bool datalog_has_card() { return s_mounted; }

uint32_t datalog_total_mb() {
    if (!s_mounted) return 0;
    return (uint32_t)(SD.totalBytes() / (1024ULL * 1024ULL));
}

uint32_t datalog_free_mb() {
    if (!s_mounted) return 0;
    uint64_t total = SD.totalBytes(), used = SD.usedBytes();
    if (used > total) return 0;
    return (uint32_t)((total - used) / (1024ULL * 1024ULL));
}

// ===========================================================================
//  FILENAME SANITISING
// ===========================================================================
static void sanitise_name(const char* in, char* out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outlen - 1; i++) {
        char c = in[i];
        if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32) c = '_';
        out[j++] = c;
    }
    while (j > 0 && (out[j - 1] == '.' || out[j - 1] == ' ')) j--;
    out[j] = '\0';

    if (j == 0) snprintf(out, outlen, "test_%lu", (unsigned long)(millis() / 1000));
}

// ===========================================================================
//  RECORDING
// ===========================================================================
bool datalog_start(const char* name) {
    datalog_stop();

    char clean[TESTNAME_MAX + 1];
    sanitise_name(name ? name : "", clean, sizeof(clean));
    snprintf(s_name, sizeof(s_name), "%s", clean);

    s_count  = 0;
    s_buflen = 0;

    if (!s_mounted) {
        s_ram_head = 0;
        s_ram_wrap = false;
        s_recording = (s_ram != nullptr);
        return s_recording;
    }

    char path[TESTNAME_MAX + 24];
    snprintf(path, sizeof(path), "%s/%s.csv", LOG_DIR, clean);

    // Overwrite rather than append: appending would splice two tests together.
    if (SD.exists(path)) SD.remove(path);

    s_file = SD.open(path, FILE_WRITE);
    if (!s_file) {
        Serial.printf("[datalog] Could not create %s\n", path);
        return false;
    }

    // Header names every unit so the CSV is self-describing.
    s_file.printf("timestamp_ms,force_%s,current_A,driver_temp_C\n",
                  (g_settings.units == Units::Newton) ? "N" : "kg");
    s_recording = true;
    Serial.printf("[datalog] Recording to %s\n", path);
    return true;
}

static void flush_buf() {
    if (!s_buflen || !s_file) return;
    s_file.write((const uint8_t*)s_buf, s_buflen);
    s_buflen = 0;
}

void datalog_add(uint32_t t_ms, float force_kg, float current_a,
                 float drv_temp_c, bool temp_valid) {
    if (!s_recording) return;

    // Force in the user's chosen unit so the CSV matches what was on screen.
    const float v = (g_settings.units == Units::Newton) ? kg_to_n(force_kg) : force_kg;

    if (!s_mounted) {
        if (!s_ram) return;
        s_ram[s_ram_head].t_ms   = t_ms;
        s_ram[s_ram_head].kg     = v;
        s_ram[s_ram_head].amps   = current_a;
        s_ram[s_ram_head].temp_c = temp_valid ? drv_temp_c : NAN;
        s_ram_head++;
        if (s_ram_head >= RAM_FALLBACK_SAMPLES) { s_ram_head = 0; s_ram_wrap = true; }
        s_count++;
        return;
    }

    char line[64];
    int n;
    if (temp_valid)
        n = snprintf(line, sizeof(line), "%lu,%.3f,%.2f,%.1f\n",
                     (unsigned long)t_ms, v, current_a, drv_temp_c);
    else
        n = snprintf(line, sizeof(line), "%lu,%.3f,%.2f,\n",
                     (unsigned long)t_ms, v, current_a);
    if (n <= 0 || n >= (int)sizeof(line)) return;

    if (s_buflen + (size_t)n >= sizeof(s_buf)) flush_buf();
    memcpy(s_buf + s_buflen, line, n);
    s_buflen += n;
    s_count++;
}

void datalog_stop() {
    if (!s_recording) return;
    s_recording = false;

    if (s_mounted && s_file) {
        flush_buf();
        s_file.close();
        Serial.printf("[datalog] Stopped. %lu samples in %s.csv\n",
                      (unsigned long)s_count, s_name);
    }
}

bool        datalog_is_recording() { return s_recording; }
uint32_t    datalog_sample_count() { return s_count; }
const char* datalog_current_name() { return s_name; }

// ===========================================================================
//  MAINTENANCE
// ===========================================================================
int datalog_file_count() {
    if (!s_mounted) return 0;
    File dir = SD.open(LOG_DIR);
    if (!dir) return 0;
    int n = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) n++;
        f.close();
    }
    dir.close();
    return n;
}

int datalog_erase_all() {
    if (!s_mounted) return 0;
    datalog_stop();

    File dir = SD.open(LOG_DIR);
    if (!dir) return 0;

    static char names[64][40];
    int n = 0;
    for (File f = dir.openNextFile(); f && n < 64; f = dir.openNextFile()) {
        if (!f.isDirectory()) snprintf(names[n++], sizeof(names[0]), "%s", f.name());
        f.close();
    }
    dir.close();

    int removed = 0;
    for (int i = 0; i < n; i++) {
        char path[64];
        if (names[i][0] == '/') snprintf(path, sizeof(path), "%s", names[i]);
        else                    snprintf(path, sizeof(path), "%s/%s", LOG_DIR, names[i]);
        if (SD.remove(path)) removed++;
    }
    Serial.printf("[datalog] Erased %d file(s)\n", removed);
    return removed;
}
