# Firmware

Two boards run their own firmware and talk over a 115200-baud UART.

| Folder | Board | What it does |
|---|---|---|
| [`rig/Force_Control_Rig_V3`](rig/Force_Control_Rig_V3) | Arduino Nano ESP32 (on PCB V2.1) | 50 Hz force loop, safety supervision, telemetry. Rev 6.2. |
| [`hmi/TouchScreenESP32Firmware`](hmi/TouchScreenESP32Firmware) | ESP32-S3 5" touchscreen | LVGL v8 UI, test sequencing, microSD logging, Wi-Fi download. v1.3.1. |
| [`tools/`](tools) | Arduino Nano ESP32 | Calibration harness and stand-alone load-cell calibration sketch |

## Toolchain

- Arduino IDE 2.x with the **esp32 by Espressif** board package.
- Rig: board **Arduino Nano ESP32**. No extra libraries beyond the core.
- HMI: libraries **ESP32_Display_Panel**, **ESP32_IO_Expander**, **esp-lib-utils**
  and **lvgl 8.x**; versions and the exact board settings (PSRAM, partition
  scheme, USB CDC) are in `hmi/TouchScreenESP32Firmware/BUILD_NOTES.md`.
  `lv_conf.h` ships with the sketch; do not use the one inside the lvgl library.

## Flashing order

1. Flash the **rig** board with nothing connected to the actuator. Open the
   serial monitor (115200) and send `?`: the command list confirms it is alive;
   `R` prints the load-cell reading.
2. Flash the **touchscreen**. On boot it shows the splash, then Home; the
   status panel turns to *Ready* once telemetry from the rig arrives.

## Things you must change for your build

| Where | What |
|---|---|
| `rig/.../Force_Control_Rig_V3.ino` | `CAL_SLOPE_RATIOMETRIC` and `CAL_INTERCEPT_KG`: the published values belong to the authors' load cell. Calibrate yours ([`../calibration`](../calibration)). |
| `rig/.../Force_Control_Rig_V3.ino` | `MAX_SETPOINT_KG` (90) and `OVERLOAD_TRIP_KG` (95) if your load cell or actuator differs. |
| `hmi/.../src/app/webserver.h` | `WIFI_AP_PASS_DEFAULT`: change the Wi-Fi password before deployment. |
| `hmi/.../TouchScreenESP32Firmware.ino` | `ENABLE_SCREENSHOTS` 0 for normal use. |

The serial command set is documented in [`../docs/serial_protocol.md`](../docs/serial_protocol.md).
