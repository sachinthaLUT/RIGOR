#include "screenshot.h"
#include "datalog.h"
#include <Arduino.h>
#include <SD.h>
#include <lvgl.h>
#include "../../lvgl_v8_port.h"

// Needs  #define LV_USE_SNAPSHOT 1  in the lv_conf.h that LVGL itself uses
// (Arduino/libraries/lv_conf.h, next to the lvgl folder). Without it this file
// still compiles, but 'p' only prints a reminder instead of saving a BMP.
#if LV_USE_SNAPSHOT

static const char* SHOT_DIR = "/shots";

// RGB565 -> 8-bit channels, exact rounding.
static inline void rgb565_to_888(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (uint8_t)((((c >> 11) & 0x1F) * 527 + 23) >> 6);
    g = (uint8_t)((((c >> 5)  & 0x3F) * 259 + 33) >> 6);
    b = (uint8_t)((( c        & 0x1F) * 527 + 23) >> 6);
}

static void put_u16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put_u32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }

// Next free /shots/scr_NN.bmp
static bool next_name(char* path, size_t n) {
    if (!SD.exists(SHOT_DIR)) SD.mkdir(SHOT_DIR);
    for (int i = 0; i < 100; i++) {
        snprintf(path, n, "%s/scr_%02d.bmp", SHOT_DIR, i);
        if (!SD.exists(path)) return true;
    }
    return false;
}

bool screenshot_take_locked(char* out, size_t outlen) {
    if (out && outlen) out[0] = '\0';
    if (!datalog_has_card()) { Serial.println("[shot] No SD card - cannot save."); return false; }
    if (datalog_is_recording()) { Serial.println("[shot] A test is recording - try again after it."); return false; }

    // Make sure pending layout changes are applied before rendering.
    lv_obj_update_layout(lv_scr_act());
    lv_obj_update_layout(lv_layer_top());

    lv_img_dsc_t* base = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!base) { Serial.println("[shot] Out of memory for the screen snapshot."); return false; }

    // Modals, keypads and toasts live on the top layer; capture it with alpha.
    lv_img_dsc_t* top = nullptr;
    if (lv_obj_get_child_cnt(lv_layer_top()) > 0)
        top = lv_snapshot_take(lv_layer_top(), LV_IMG_CF_TRUE_COLOR_ALPHA);

    const int W = base->header.w, H = base->header.h;
    const uint16_t* px = (const uint16_t*)base->data;                    // RGB565
    const uint8_t*  tp = top ? top->data : nullptr;                      // [lo, hi, alpha]
    const int tw = top ? top->header.w : 0, th = top ? top->header.h : 0;

    char path[40];
    if (!next_name(path, sizeof(path))) {
        Serial.println("[shot] /shots is full (100 files).");
        lv_snapshot_free(base); if (top) lv_snapshot_free(top); return false;
    }
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[shot] Could not create %s\n", path);
        lv_snapshot_free(base); if (top) lv_snapshot_free(top); return false;
    }

    // ---- 24-bit BMP header (bottom-up rows, padded to 4 bytes) -----------
    const uint32_t rowBytes = ((uint32_t)W * 3 + 3) & ~3u;
    const uint32_t imgBytes = rowBytes * H;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    put_u32(hdr + 2, 54 + imgBytes);
    put_u32(hdr + 10, 54);
    put_u32(hdr + 14, 40);
    put_u32(hdr + 18, W);
    put_u32(hdr + 22, H);
    put_u16(hdr + 26, 1);
    put_u16(hdr + 28, 24);
    put_u32(hdr + 34, imgBytes);
    put_u32(hdr + 38, 3780);            // 96 dpi
    put_u32(hdr + 42, 3780);
    f.write(hdr, sizeof(hdr));

    uint8_t* row = (uint8_t*)malloc(rowBytes);
    if (!row) { f.close(); SD.remove(path); lv_snapshot_free(base); if (top) lv_snapshot_free(top); return false; }
    memset(row, 0, rowBytes);

    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint8_t r, g, b;
            rgb565_to_888(px[y * W + x], r, g, b);
            if (tp && x < tw && y < th) {
                const uint8_t* q = tp + 3 * (y * tw + x);
                const uint8_t a = q[2];
                if (a) {
                    uint8_t r2, g2, b2;
                    rgb565_to_888((uint16_t)(q[0] | (q[1] << 8)), r2, g2, b2);
                    r = (uint8_t)((r2 * a + r * (255 - a) + 127) / 255);
                    g = (uint8_t)((g2 * a + g * (255 - a) + 127) / 255);
                    b = (uint8_t)((b2 * a + b * (255 - a) + 127) / 255);
                }
            }
            row[3 * x + 0] = b; row[3 * x + 1] = g; row[3 * x + 2] = r;   // BMP is BGR
        }
        f.write(row, rowBytes);
    }
    free(row);
    f.close();
    lv_snapshot_free(base);
    if (top) lv_snapshot_free(top);

    Serial.printf("[shot] Saved %s (%dx%d)\n", path, W, H);
    if (out) snprintf(out, outlen, "%s", path);
    return true;
}

bool screenshot_take(char* out, size_t outlen) {
    lvgl_port_lock(-1);
    const bool ok = screenshot_take_locked(out, outlen);
    lvgl_port_unlock();
    return ok;
}

void screenshot_poll() {
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == 'p' || c == 'P') screenshot_take(nullptr, 0);
    }
}

static void splash_cb(lv_timer_t*) {
    // Timer callbacks run on the LVGL task, which already holds the lock.
    // Repeat count 1: LVGL deletes the timer itself after this call.
    screenshot_take_locked(nullptr, 0);
}

void screenshot_arm_splash(unsigned delay_ms) {
    lv_timer_t* t = lv_timer_create(splash_cb, delay_ms, nullptr);
    lv_timer_set_repeat_count(t, 1);
}

#else   // ---- LV_USE_SNAPSHOT is 0: stubs, so the sketch still builds --------

static void shot_disabled() {
    Serial.println("[shot] Disabled: set  #define LV_USE_SNAPSHOT 1  in "
                   "Arduino/libraries/lv_conf.h and rebuild.");
}
bool screenshot_take_locked(char* out, size_t outlen) {
    if (out && outlen) out[0] = '\0';
    shot_disabled();
    return false;
}
bool screenshot_take(char* out, size_t outlen) { return screenshot_take_locked(out, outlen); }
void screenshot_poll() {
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == 'p' || c == 'P') shot_disabled();
    }
}
void screenshot_arm_splash(unsigned) {}

#endif  // LV_USE_SNAPSHOT
