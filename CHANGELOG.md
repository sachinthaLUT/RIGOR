# Changelog

## [1.0.0] — first public release

Configuration characterised in the paper.

- Mechanical: T-slot frame, 1500 N / 100 mm / 5 mm/s actuator, 100 kg in-line load cell.
- Electronics: controller PCB V2.1 (ADS1220, ACS773, Pololu G2 18v25, Arduino Nano ESP32).
- Rig firmware rev 6.2: PI force control with fine-zone pulses, tare (`O1`/`O0`),
  buzzer volume (`V0..2`), raw counts and tare in telemetry.
- Touchscreen firmware v1.3.1: static, stepped and dynamic modes, calibration
  screen with tare and raw-count capture, microSD logging, Wi-Fi download.
