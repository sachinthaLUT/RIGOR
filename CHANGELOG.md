# Changelog

## [1.0.0] — first public release

The configuration characterised in the paper.

- **Mechanical:** T-slot frame; VEVOR OK648 actuator (1500 N, 100 mm, 5 mm/s);
  ATO-LCC-DYMH-103 100 kg in-line load cell; 3D-printed control enclosure.
- **Electronics:** controller PCB **V2.1** (Arduino Nano ESP32, Olimex BB-ADS1220,
  ACS773 current sensor, Pololu G2 18v25 driver). Gerbers for V2.1 as built.
- **Rig firmware** `Force_Control_Rig_V4` (firmware revision 6.2): PI force
  control with fine-zone pulses, tare (`O1`/`O0`), buzzer volume (`V0..2`), raw
  counts and tare in telemetry.
- **Touchscreen firmware** v1.3.1: static, stepped and dynamic modes, calibration
  screen with tare and raw-count capture, microSD logging, Wi-Fi download.

### Unreleased

- **PCB V2.2** (KiCad project and `RIGOR_PCB_V2.2_gerbers.zip`): touchscreen
  UART connector J2 re-pinned. Not yet fabricated or tested.
