# Calibration

The load cell is calibrated as a transducer **through the rig's own electronics**
(same PCB, same ADC settings), so the result covers the whole measurement chain.
The procedure follows ISO 7500-1; the full method and results are in the paper.

## What you need

- A reference: a universal testing machine (UTM) or a force-proving instrument
  (ISO 376) covering 0–1 kN in compression.
- The rig PCB connected over USB, running
  [`../firmware/tools/Force_Rig_Calibration_Harness_V1`](../firmware/tools) (or the
  touchscreen's Calibration screen, which shows and averages the raw counts `N=`).
- [`Force_Rig_Calibration_Workbook.xlsx`](Force_Rig_Calibration_Workbook.xlsx).

## Procedure (summary)

1. Warm the electronics up (≥ 30 min); record ambient temperature.
2. Mount the cell co-axially between the reference platens.
3. Preload three times to the maximum calibration force.
4. Run three increasing series (0, 10, 20 … 100 kg) and one decreasing series,
   recording the reference force and the raw counts at each level.
5. Read the zero 30 s after removing the force at the end of the decreasing series.
6. Enter the data in the workbook sheet `LC_Reference`; the workbook fits
   `F = s·D + c` (kg from counts), computes the ISO 7500-1 errors and class, and
   the uncertainty budget. Put the reference's own error/uncertainty in `Setup!B25`.
7. Copy `s` and `c` into the rig firmware (`CAL_SLOPE_RATIOMETRIC`,
   `CAL_INTERCEPT_KG`) and re-flash.

## Tare in the rig

Once installed, the cell may read a small offset unloaded because the rig loads
it differently from the reference. Zero it with **Calibration → Tare** on the
touchscreen (or send `O1`; `O0` clears it). The tare is stored in flash and is
applied on top of the calibration constants, which stay unchanged.

## Recalibration

Recalibrate after replacing the load cell, the PCB or the ADC settings, after an
overload event, and at least annually.
