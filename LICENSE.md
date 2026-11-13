# Licences

RIGOR is open hardware. Different parts of the repository carry different
licences, chosen for the kind of material each part is.

| Path | Licence | SPDX |
|---|---|---|
| `hardware/` (CAD, drawings, 3MF, KiCad, gerbers, BOM) | CERN Open Hardware Licence v2, Permissive | `CERN-OHL-P-2.0` |
| `firmware/`, `analysis/` (source code and scripts) | MIT | `MIT` |
| `docs/`, `photos/`, `calibration/`, README files | Creative Commons Attribution 4.0 | `CC-BY-4.0` |

Full texts are in [`LICENSES/`](LICENSES).

Copyright (c) 2026 Sachintha Alwis Weerasinghe, Adam Kłodowski, Grzegorz
Orzechowski, Aki Mikkola.

## Third-party material (not covered by the licences above)

- **Libraries.** The firmware builds against libraries that keep their own
  licences, including LVGL (MIT) and ESP32_Display_Panel (Apache-2.0). The files
  `lvgl_v8_port.h/.cpp` and the `esp_*_conf.h` templates in
  `firmware/hmi/TouchScreenESP32Firmware/` come from the ESP32_Display_Panel
  examples (CC0-1.0 / Apache-2.0).
- **Datasheets** in `hardware/electronics/datasheets/` are the property of their
  manufacturers and are included for convenience only.
- **Supplier CAD models** (the IDEC e-stop STEP file, and the actuator and load-cell
  models inside the CAD archives) belong to their manufacturers.
