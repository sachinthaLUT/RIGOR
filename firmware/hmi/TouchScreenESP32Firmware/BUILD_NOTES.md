# Touchscreen firmware: build and design notes

Setup (libraries, configuration files, board settings) is in
[`README.md`](README.md). This file records why the firmware behaves the way
it does.

## Bring-up checklist

1. Flash the rig first (`firmware/rig/Force_Control_Rig_V4`). Its power-on
   self-test prints `[HMI] touchscreen CONNECTED` once the display talks to it.
2. Flash the display. Check that every screen renders and Settings scrolls.
3. Settings → SD row reports the card's free space. Cards of 32 GB and larger
   ship as exFAT and must be reformatted FAT32. Without a card, logging falls
   back to a 3000-sample RAM buffer.
4. Static: Custom → 5 kg → START. Watch the stage indicator.
5. Dynamic: 5 → 10 kg, 3 s dwells, 3 cycles, named run → START. Watch the chart.
6. Download: join the rig's Wi-Fi with a phone and fetch the file.
7. **Test the failure path:** start a run and unplug the link cable. The display
   should report the lost link within about 1 s; the rig stops on its own after 5 s
   and, with *retract on link loss* enabled, backs off.

## Design notes

**Watchdog on the rig, not the display.** It must work when the display is
the part that failed. The retract-on-loss setting lives in the display's
Settings but is sent to the rig (`W0`/`W1`) and re-sent on every reconnect,
because a rebooted rig reverts to its compiled-in default.

**Square-wave dynamic loading only.** The loop is tuned to settle, not to
track; a commanded sine would come back lagged and attenuated. The chart draws
ramp, dwell, ramp, dwell with dashed lines at the commanded levels, so the gap
between commanded and achieved force stays visible. Cycle time is entered in
seconds; frequency is shown for information. Hold timers start when the force
arrives, not when the command is sent, and a cycle counts on returning to the
lower level.

**PAUSE sends `S`.** The motor stops and the self-locking leadscrew holds
position, but force is then not regulated; the screen says so instead of
showing a number that looks controlled.

**Download is blocked during a test.** Wi-Fi and the RGB panel share memory
bandwidth, and the access point would steal time from the cycler. The web
server runs only while the download dialog is open, serves a self-contained
page (the phone has no internet on this network) and guards against path
traversal.

**Logging is buffered** about 1 kB at a time, because SD writes block for
milliseconds and per-sample writes would stall the UI. File names typed on the
touchscreen are sanitised for FAT rather than rejected.

**Backlight** is fixed at 100 %; `hal_set_backlight()` in the `.ino` overrides
the weak stub in `src/app/hal.cpp`.

## If it does not compile

| Symptom | Fix |
|---|---|
| `multiple definition of screenshot_take` | Only `src/app/screenshot.cpp` may exist; delete any copy under `src/ui/`. |
| Fonts missing / tiny text | `lv_conf.h` is not the one in your Arduino libraries folder; see README. |
| `LV_SYMBOL_PAUSE` undefined | Older LVGL v8 release: replace with `"II"`. |
| `SD.totalBytes()` unavailable | Old esp32 core: return 0 from the two SD-size helpers (only the free-space display uses them). |
