# Calibration

The load cell is calibrated as a transducer **through the rig's own electronics**
(same PCB, same ADC settings), so the result covers the whole measurement chain.
The procedure follows ISO 7500-1; method and results are in the paper.

| File | Contents |
|---|---|
| [`Force_Rig_Calibration_Workbook.xlsx`](Force_Rig_Calibration_Workbook.xlsx) | Calibration fit, ISO 7500-1 assessment, all characterisation tests (T1–T13) and the uncertainty budget. The analysis script reads its numbers from here. |
| [`Load_Cell_Calibration_Data.xlsx`](Load_Cell_Calibration_Data.xlsx) | Raw counts and reference forces from the UTM calibration session |

## What you need

- A reference: a universal testing machine (UTM) or a force-proving instrument
  (ISO 376) covering 0–1 kN in compression.
- The rig PCB connected over USB, running
  [`../firmware/tools/LoadCellCalibrationFirmware`](../firmware/tools/LoadCellCalibrationFirmware)
  (raw counts at each force level). The characterisation tests in the workbook
  were run with [`../firmware/tools/ForceRigCalibrationHarness`](../firmware/tools/ForceRigCalibrationHarness).
  The touchscreen's Calibration screen also shows and averages the raw counts `N=`.

## Procedure (summary)

1. Warm the electronics up (≥ 30 min); record the ambient temperature.
2. Mount the cell co-axially between the reference platens.
3. Preload three times to the maximum calibration force.
4. Run three increasing series (0, 10, 20 … 100 kg) and one decreasing series,
   recording the reference force and the raw counts at each level.
5. Read the zero 30 s after removing the force at the end of the decreasing series.
6. Enter the data in the workbook sheet `LC_Reference`. The workbook fits
   `F = s·D + c` (kg from counts), computes the ISO 7500-1 errors and class, and
   the uncertainty budget. Put the reference's own error or uncertainty in `Setup!B25`.
7. Copy the slope and the rig intercept into the rig firmware
   (`CAL_SLOPE_RATIOMETRIC`, `CAL_INTERCEPT_KG`) and re-flash. If anything rested
   on the cell during calibration that is absent in the rig (here a 0.2214 kg
   plate), add its mass to the intercept: `LC_Reference!B28` does this.

## Tare in the rig

Once installed, the cell may read a small offset unloaded because the rig
loads it differently from the reference. Zero it with **Calibration → Tare**
on the touchscreen (or send `O1`; `O0` clears it). The tare is stored in flash
and applied on top of the calibration constants, which stay unchanged.

## Recalibration

Recalibrate after replacing the load cell, the PCB or the ADC settings, after
an overload event, and at least annually.
