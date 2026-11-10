# Mechanical design

| Folder | Contents | Format |
|---|---|---|
| [`cad/`](cad) | Native CAD assembly and parts, for editing | TBC (e.g. SolidWorks / Fusion / FreeCAD) |
| [`step/`](step) | Neutral 3D models of the assembly and every custom part | STEP AP214 |
| [`drawings/`](drawings) | 2D manufacturing drawings of the custom parts | PDF |
| [`stl/`](stl) | Printable parts (enclosure, bezels, covers) | STL |

## Custom parts

| ID | Part | Process | Material |
|---|---|---|---|
| M5 | Actuator rear bracket | TBC (machined / laser-cut) | TBC |
| M6 | Actuator front support | TBC | TBC |
| M7 | Loading tup | Turned | TBC |
| M8 | Reaction plate | TBC | TBC |
| E12 | Electronics enclosure | FDM print | TBC |

## Conventions

- File names: `RIG-<ID>_<name>_r<rev>.<ext>`, e.g. `RIG-M7_tup_r1.step`.
- One STEP of the full assembly: `RIG-ASM_rigor_r1.step`.
- Drawings show only what a workshop needs: critical dimensions, threads,
  tolerances on the load-line features (co-axiality of rod, cell and tup).
- Units: millimetres.
