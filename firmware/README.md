# Firmware

Two boards run their own firmware and talk over a 115200-baud UART.

| Folder | Board | What it does | Version |
|---|---|---|---|
| [`rig/Force_Control_Rig_V4`](rig/Force_Control_Rig_V4) | Arduino Nano ESP32 on the controller PCB | 50 Hz force loop, safety supervision, telemetry | firmware rev 6.2 |
| [`hmi/TouchScreenESP32Firmware`](hmi/TouchScreenESP32Firmware) | Sunton ESP32-8048S050 (ESP32-S3, 5", 800×480) | LVGL v8 UI, test sequencing, microSD logging, Wi-Fi download | v1.3.1 |
| [`tools/ForceRigCalibrationHarness`](tools/ForceRigCalibrationHarness) | Arduino Nano ESP32 on the controller PCB | Runs the characterisation tests (T1–T12) over USB and prints CSV for the workbook | V1 |
| [`tools/LoadCellCalibrationFirmware`](tools/LoadCellCalibrationFirmware) | Arduino Nano ESP32 on the controller PCB | Prints raw ADC counts for the UTM calibration of the load cell | — |

## Toolchain

- **Arduino IDE 2.x** with the **esp32 by Espressif** board package.
- **Rig and tools:** board *Arduino Nano ESP32*. No libraries beyond the core.
- **Touchscreen:** see [`hmi/TouchScreenESP32Firmware/README.md`](hmi/TouchScreenESP32Firmware/README.md)
  for libraries, board settings and the configuration files.

## Flashing order

1. Flash the **rig** board with the actuator disconnected. Open the serial
   monitor at 115200 baud and send `?`: the command list confirms it is alive;
   `R` prints the load-cell reading.
2. Flash the **touchscreen**. It shows the splash, then Home; the status panel
   turns to *Ready* once telemetry from the rig arrives.

## Things to change for your build

| File | What |
|---|---|
| `rig/Force_Control_Rig_V4/Force_Control_Rig_V4.ino` | `CAL_SLOPE_RATIOMETRIC` and `CAL_INTERCEPT_KG`: the published values belong to the authors' load cell. Calibrate yours ([`../calibration`](../calibration)). |
| `rig/Force_Control_Rig_V4/Force_Control_Rig_V4.ino` | `MAX_SETPOINT_KG` (90) and `OVERLOAD_TRIP_KG` (95) if your load cell or actuator differs. |
| `tools/ForceRigCalibrationHarness/ForceRigCalibrationHarness.ino` | `CAL_SLOPE_KG_PER_COUNT` and `CAL_INTERCEPT_KG`, the same values as the rig. |
| `hmi/TouchScreenESP32Firmware/src/app/webserver.h` | `WIFI_AP_SSID_DEFAULT` and `WIFI_AP_PASS_DEFAULT`: **set your own Wi-Fi password.** The default in this public repository is known to everyone. |

The serial command set and the telemetry format are documented in
[`../docs/serial_protocol.md`](../docs/serial_protocol.md).
