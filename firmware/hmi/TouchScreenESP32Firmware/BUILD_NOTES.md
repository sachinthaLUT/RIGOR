# Force Rig — firmware rebuild

Two sketches. Flash **the rig first**, then the display.

```
ForceRigCompleteFirmware/
├── Force_Control_Rig_V3/        ← Nano ESP32 (rig)
│   └── Force_Control_Rig_V3.ino
└── TouchScreenESP32Firmware/    ← ESP32-S3 (display)
    ├── TouchScreenESP32Firmware.ino
    └── src/{app,link,ui}/
```

Your root config files (`lv_conf.h`, `esp_utils_conf.h`, panel configs, LVGL
port) are **unchanged** — keep the ones you have.

---

## Before you compile

**Libraries.** Two new dependencies, both bundled with the ESP32 core, nothing
to install: `SD` + `SPI` (datalog) and `WiFi` + `WebServer` (download page).

**`LINK_DEBUG_RX` is now 0.** The per-line RX spam is off.

**SD card.** Wired to GPIO 10/11/12/13 — free on your board and the standard
Sunton slot pinout, so an on-board slot needs no wiring. Your 16 GB card is
almost certainly FAT32 already and will just work. **Cards ≥ 32 GB ship as
exFAT and must be reformatted FAT32** — that warning appears in `datalog.h`,
in the serial log on failed mount, on the Settings SD row, in the download
dialog, and on the web page.

**No card is fine.** Logging falls back to a 3,000-sample RAM ring buffer and
the UI says which mode it's in, so nothing is blocked waiting for hardware.

---

## Rig — V3

| Change | Detail |
|---|---|
| `Q` | Beep the buzzer. The display has none; the rig's is where an alarm belongs. |
| `W0` / `W1` | On HMI loss: hold position / retract 5 mm. Default **hold**. |
| `F<hz>` | Telemetry rate, clamped 1–50. 4 Hz idle, 50 Hz while logging. |
| **HMI watchdog** | In `MODE_RUN`, 5 s of silence from the display → stop, then optionally retract. |
| Blocking-move fix | `jogMove()` and `doRelativeRetract()` now emit telemetry and poll the link while they block. |

That last one was a real bug: a 1 s retract exceeded the display's 1 s link
timeout, so the screen flashed "No link" mid-move and refused the next press.

**Why the watchdog is on the rig, not the display:** it has to work when the
display is the thing that died. A 100-cycle fatigue run is ~30 minutes
unattended under load.

**Why the setting is transmitted:** the flag lives in display Settings but is
sent with `W`, and `link.cpp` **re-sends it on every reconnect** — a rebooted
rig reverts to its compiled-in default and wouldn't otherwise know.

---

## Display — what's new

**`app/datalog.*`** — SD logging. Buffered ~1 KB at a time because SD writes
block for milliseconds and per-sample writes would stutter the UI and delay the
cycler. Filenames sanitised for FAT rather than rejected, so a name typed on a
touchscreen can't lose you a run.

**`app/webserver.*`** — SoftAP + file browser. Runs **only while the dialog is
open**. The page lists every file with size, sorts by name or newest, shows free
space, downloads with one tap, and has erase-all behind a confirm. Self-contained
HTML — a phone on this AP has no internet, so anything external would fail to
load. Path-traversal guarded on the download handler.

**`ui/screen_dynamic2.cpp`** — the new Dynamic Loading screen. Square wave only,
five parameters, live chart, cycle counter, START→PAUSE→CONTINUE, download.

**`ui/widgets.*`** — added `w_keypad()` (numeric, SET greys out when out of
range), `w_textkb()` (test names, FAT-invalid characters absent from the
keyboard), and `w_page()` (the scrolling Settings container).

**Renamed:** old Dynamic → **Stepped Loading**, and it gained the Jog/Retract
row that was in the render but never coded.

**Titles:** "Jog +", "Jog −", **"Retract"** everywhere, title case, on Static,
Stepped and Diagnosis.

**Home:** six tiles exactly fill the 3×2 grid now that About moved into
Settings, and the freed column is a full-height live status panel. The STOP tile
is gone — nobody stands on Home while a test runs.

**Settings:** scrolls, with a visible bar (the only cue on a touchscreen that
more content exists). Adds the retract-on-loss switch, SD status + Erase, and
About at the bottom.

---

## Dynamic loading — the parts worth knowing

**Square wave only, and that's not a compromise.** Your loop is tuned to
*settle*, not *track*. A commanded sine would come back lagged and attenuated —
data that looks scientific and isn't. The chart draws the trace as
ramp–dwell–ramp–dwell with dashed lines at the commanded levels, so the gap
between commanded and achieved stays visible.

**Cycle time, not Hz.** Hz is shown as derived info. The screen also states
estimated total duration before you start.

**Hold timers start on arrival**, not on send — otherwise ramp time eats the
dwell and short steps barely load the specimen.

**A cycle counts on returning to bottom**, so the number means completed loops.

**PAUSE sends `S`.** Motor off, leadscrew self-locks, actuator holds position —
which is what you asked for. But force is then *not regulated*, so the label
says "PAUSED - force is not being controlled" rather than showing a number that
looks live. Expect slight drift on a long pause and a settling transient on
CONTINUE.

**Download is blocked mid-test.** WiFi and the RGB panel share memory
bandwidth, and the AP would steal time from the cycler.

---

## Brightness

`hal_set_backlight()` in the `.ino` has two flags at the top:

```c
#define BL_SCALE_255  0   // 1 = the API wants 0..255, not 0..100
#define BL_GAMMA      0   // 1 = apply a perceptual curve
```

Your symptom (usable only in the top 10%) most likely means the API wants
0–255, so "100%" was really 39%. **Try `BL_SCALE_255` first**, then `BL_GAMMA`
if it's still bunched. One at a time.

---

## Build expectations

I can't compile ESP32/LVGL here, so budget one cycle. Verified by inspection:
brace/comment/string balance on all 27 files, every cross-module API declared
*and* defined, the ScreenId enum aligned 1:1 with the dispatch table, all
globals declared before first use, and **every command the display sends is
both validated and handled by the rig** (checked programmatically).

Two real ordering bugs were caught and fixed during the build — `lastHmiRxMs`
and `hmiTelemetryPeriodMs` used above their definitions. Arduino auto-prototypes
functions but not globals, so those wouldn't have compiled.

Most likely remaining issues:

1. `LV_SYMBOL_PAUSE` missing on an older LVGL v8 point release → use `"II"`.
2. `SD.totalBytes()` unavailable on an old core → return 0 from those two
   helpers; only the free-space display depends on them.
3. `lv_obj_set_style_size()` signature drift in `screen_dynamic2.cpp`'s chart
   setup on newer v8 — takes two args in some versions.

## Bring-up order

1. Flash rig, confirm POST shows the Touchscreen line and `[HMI] touchscreen CONNECTED`.
2. Flash display. Check all seven screens render and Settings scrolls.
3. Settings → SD row should report your card's free space.
4. Static: Custom → type 5 → START. Watch the stage dots.
5. Dynamic: 5→10 kg, 3 s dwells, 3 cycles, name it, START. Watch the chart.
6. Download: connect a phone, confirm the file lists and downloads.
7. **Then test the failure path deliberately** — start a run and pull the link
   cable. The display should abort within 1.5 s, and with the retract setting on,
   the rig should back off by itself. That path matters more than any of the
   happy ones.
