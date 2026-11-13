# Bill of materials

[`RIGOR_BOM.xlsx`](RIGOR_BOM.xlsx) is the full bill of materials for one rig:
a summary that matches Table 3 of the paper, then every part with
manufacturer, part number, supplier, quantity, price and links to the product
page and datasheet. [`BOM.csv`](BOM.csv) is a plain-text export of the same
list that GitHub can display.

- Prices are in EUR, **excluding 25.5 % Finnish VAT and shipping**, as purchased
  in 2026. Quantities are as ordered and include spares.
- **Total for one rig: about €537** (rounded up, €538). Frame and fixtures were
  machined in-house from stock material and are not costed.
- Sections E and F (frame, fixtures, fasteners, cabling) give specifications
  only. Custom parts reference their drawings in
  [`../mechanical/drawings`](../mechanical/drawings).
- PCB components are also in the KiCad project
  ([`../electronics/kicad`](../electronics/kicad)).

## Substitutions

| Part | What matters if you substitute |
|---|---|
| Actuator | Self-locking leadscrew (holds load unpowered), 12 V, force ≥ 1.5 × your maximum test load, built-in limit switches. A faster actuator extends the dynamic range but makes fine force control harder. |
| Load cell | Tension/compression, threaded both ends, capacity matched to the test load. Recalibrate after any change. |
| Motor driver | PWM + DIR interface at 3.3 V logic, a fault output, continuous rating well above the actuator's stall current. |
| Touchscreen | Must be supported by ESP32_Display_Panel; the board config in the HMI firmware must match (Sunton ESP32-8048S050 as built). |
