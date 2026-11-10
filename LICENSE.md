# Licences

RIGOR is open hardware. Different parts of the repository carry different
licences, chosen for the kind of material each part is.

| Path | Licence | SPDX |
|---|---|---|
| `hardware/` (CAD, drawings, STL, KiCad, gerbers) | CERN Open Hardware Licence v2, Permissive | `CERN-OHL-P-2.0` |
| `firmware/`, `analysis/` (source code and scripts) | MIT | `MIT` |
| `docs/`, `calibration/`, `analysis/data/`, images, README files | Creative Commons Attribution 4.0 | `CC-BY-4.0` |

Full texts are in [`LICENSES/`](LICENSES).

Copyright (c) 2026 Sachintha Alwis Weerasinghe, Adam Kłodowski, Grzegorz
Orzechowski, Aki Mikkola.

## Third-party components

The firmware builds against libraries that keep their own licences, including
LVGL (MIT) and ESP32_Display_Panel (Apache-2.0). The files
`lvgl_v8_port.h/.cpp` in `firmware/hmi/` are taken from the ESP32_Display_Panel
examples (CC0-1.0). Datasheets in `hardware/electronics/datasheets/` (if any)
belong to their manufacturers and are not covered by the licences above.
