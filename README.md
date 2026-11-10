<p align="center">
  <img src="docs/images/logo.png" alt="RIGOR" width="260">
</p>

<p align="center">
  <b>An open, reconfigurable electromechanical rig for closed-loop compressive force testing</b><br>
  0–90 kg (≈ 883 N) · PI force control on an in-line load cell · ISO 7500-1 class 1 measurement chain
</p>

<p align="center">
  <!-- Replace XXXXXXX with the Zenodo DOI after the first release -->
  <a href="https://doi.org/10.5281/zenodo.XXXXXXX"><img src="https://img.shields.io/badge/DOI-10.5281%2Fzenodo.XXXXXXX-blue" alt="DOI"></a>
  <img src="https://img.shields.io/badge/hardware-CERN--OHL--P--2.0-green" alt="Hardware licence">
  <img src="https://img.shields.io/badge/firmware-MIT-green" alt="Firmware licence">
  <img src="https://img.shields.io/badge/docs-CC--BY--4.0-green" alt="Documentation licence">
</p>

![Assembled RIGOR rig](docs/images/assembled_rig.jpg)

RIGOR applies a commanded compressive force to a specimen and holds it, steps
through a programmed sequence, or cycles between two levels. A commodity
self-locking 12 V linear actuator is regulated on an in-line 100 kg load cell
read by a ratiometric 24-bit ADC on a custom controller board. A 5-inch
touchscreen runs the tests, logs force against time to a microSD card and serves
the logs over Wi-Fi.

This repository holds everything needed to build, calibrate and run one: the
bill of materials, CAD, PCB files, firmware, the calibration workbook and the
analysis scripts used in the paper.

## Performance (as characterised)

| Characteristic | Result |
|---|---|
| Calibrated range | 10–90 kg |
| Measurement chain | ISO 7500-1 class 1 (worst indication error 0.74 %) |
| Command-to-achieved error | ≤ 0.30 % of full scale |
| Short-term repeatability | CV ≤ 0.35 % |
| Expanded uncertainty at 40 kg (k = 2) | 0.64 % indicated, 1.78 % generated force |
| Cyclic envelope (square wave) | up to 0.25 Hz |
| Protective functions | overload, overcurrent, over-temperature, end of travel, approach timeout, e-stop, link watchdog |

Full method and results: see [Citation](#citation).

## Repository layout

```
hardware/
  bom/            Bill of materials (BOM.csv) and sourcing notes
  mechanical/     CAD (native + STEP), drawings, STL for printed parts
  electronics/    KiCad project, gerbers, schematic PDF, wiring
firmware/
  rig/            Control board (Arduino Nano ESP32): force loop and safety
  hmi/            Touchscreen (ESP32-S3, LVGL v8): UI, logging, Wi-Fi download
  tools/          Calibration harness and load-cell calibration sketches
calibration/      Calibration workbook (ISO 7500-1 assessment and uncertainty)
analysis/         MATLAB script that produces the paper's figures and tables
docs/             Assembly, wiring, operation, serial protocol, safety
```

## Getting started

1. **Source the parts** in [`hardware/bom/BOM.csv`](hardware/bom/BOM.csv).
2. **Make the custom parts** from [`hardware/mechanical`](hardware/mechanical)
   and order the PCB from [`hardware/electronics/gerbers`](hardware/electronics/gerbers).
3. **Assemble** following [`docs/assembly.md`](docs/assembly.md).
4. **Flash the firmware** following [`firmware/README.md`](firmware/README.md).
5. **Calibrate your load cell** following [`calibration/README.md`](calibration/README.md).
   The constants in the firmware belong to the authors' cell; yours will differ.
6. **Read [`docs/safety.md`](docs/safety.md)** before the first loaded run.

## Adapting the design

The frame is bolted T-slot profile, the load cell is M6-threaded at both ends
and the actuator family comes in several stroke and speed variants with the same
mounting. Test modes, limits and control parameters live in firmware. The
selection guidance in [`docs/design_parameters.md`](docs/design_parameters.md)
explains how to scale each component to a different force range.

## Citation

If you use RIGOR or its data, please cite the paper and this repository.
Citation metadata is in [`CITATION.cff`](CITATION.cff) (GitHub shows a
"Cite this repository" button for it).

> Alwis Weerasinghe, S., Kłodowski, A., Orzechowski, G., Mikkola, A. *RIGOR: An
> open, reconfigurable electric-actuator testing rig for compressive loading.*
> (submitted, 2026).

## Licence

| Part | Licence |
|---|---|
| Hardware design (CAD, drawings, PCB) | [CERN-OHL-P-2.0](LICENSES/CERN-OHL-P-2.0.txt) |
| Firmware and scripts | [MIT](LICENSES/MIT.txt) |
| Documentation, images, data | [CC BY 4.0](LICENSES/CC-BY-4.0.txt) |

See [`LICENSE.md`](LICENSE.md). Third-party libraries keep their own licences.

## Acknowledgements

Supported by the Finnish Ministry of Education and Culture's Pilot for Doctoral
Programmes (Pilot project Mathematics of Sensing, Imaging and Modeling).
Fabricated with the facilities of the Jamie Hyneman Center, LUT University,
Lappeenranta.

## Safety

This is a powered machine that stores mechanical energy in the specimen and the
load path. It is supplied as a research design without warranty. Read
[`docs/safety.md`](docs/safety.md).
