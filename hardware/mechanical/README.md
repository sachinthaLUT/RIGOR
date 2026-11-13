# Mechanical design

All CAD is in SolidWorks. Neutral exports (STEP, PDF) are listed below so the
rig can be built without SolidWorks.

| Path | Contents |
|---|---|
| [`cad/Portable Rig.zip`](cad) | Rig assembly `Portable Rig.SLDASM` with all custom parts, the actuator and the load cell |
| [`cad/Enclosure Assembly.zip`](cad) | Control enclosure: `Body.SLDPRT`, `Lid.SLDPRT`, e-stop model |
| [`drawings/`](drawings) | Manufacturing drawings of the custom parts (`.SLDDRW`; PDF exports in `drawings/pdf/`) |
| [`step/`](step) | STEP AP214 exports of the rig assembly and each custom part |
| [`3mf/`](3mf) | Print files for the enclosure body and lid |

## Custom parts

| Drawing | Part | BOM line | Process | Material |
|---|---|---|---|---|
| Actuator bracket 1 | Actuator rear bracket (clevis mount) | E4 | CNC | TBC |
| Actuator bracket 2 + BracketLid | Actuator front support and its clamp | E5 | CNC | TBC |
| LoadCellActuatorBracket | Actuator rod to load cell adaptor (M6) | E6 | CNC | TBC |
| LoadCellPusher | Loading tup (M6) | E7 | Turned | TBC |
| Force plate | Reaction plate | E8 | CNC | TBC |
| Enclosure Body / Lid | Control enclosure | D2 | FDM print | TBC |

Bought parts (T-slot profiles, gussets, fasteners) are listed in
[`../bom`](../bom).

## Notes

- Units: millimetres.
- Critical features are those on the load line: the actuator rod, adaptor,
  load cell and tup must be co-axial, and the reaction-plate face square to the
  rail. See [`../../docs/assembly.md`](../../docs/assembly.md).
