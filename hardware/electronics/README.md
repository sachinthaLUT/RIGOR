# Electronics

Controller PCB **V2.1** carries the microcontroller, the load-cell ADC, current
and temperature sensing, and the connectors for the motor driver, actuator,
e-stop and touchscreen link.

![PCB V2.1](../../docs/images/pcb_v2.1.jpg)

| Path | Contents |
|---|---|
| [`kicad/`](kicad) | KiCad project (`.kicad_pro`, `.kicad_sch`, `.kicad_pcb`) and project libraries |
| [`gerbers/`](gerbers) | Fabrication outputs, zipped, ready to upload to a PCB fab |
| `schematic.pdf` | Schematic, for reading without KiCad |
| `pcb_bom.csv` | PCB component list with values, footprints and manufacturer part numbers |
| `positions.csv` | Pick-and-place file (only if you order assembly) |
| [`datasheets/`](datasheets) | Links to key datasheets (do not commit PDFs you may not redistribute) |

Board-to-board and off-board wiring: [`../../docs/wiring.md`](../../docs/wiring.md).

## Exporting from KiCad

1. **Schematic PDF:** Schematic editor → File → Plot → PDF.
2. **Gerbers + drill:** PCB editor → File → Fabrication Outputs → Gerbers, then
   Drill Files. Zip the folder as `gerbers/RIGOR_PCB_V2.1_gerbers.zip`.
3. **BOM:** Schematic editor → Tools → Generate BOM (CSV) → `pcb_bom.csv`.
4. **Positions:** PCB editor → File → Fabrication Outputs → Component Placement.

## Known issues / next revision

- Wi-Fi antenna of the Nano ESP32 should sit clear of copper pours and the enclosure wall.
- Component values to be printed on the silkscreen legend.
