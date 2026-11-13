# Analysis

[`RIGOR_figures.m`](RIGOR_figures.m) regenerates the figures of the paper and
prints the values of its tables. It reads everything from
[`../calibration/Force_Rig_Calibration_Workbook.xlsx`](../calibration/Force_Rig_Calibration_Workbook.xlsx),
so no other data files are needed.

1. Open MATLAB (R2020a or later) in this folder.
2. Run `RIGOR_figures`.
3. Figures are written to `figures/` as PDF and PNG, and every table value to
   `figures/paper_table_values.txt`.

Settings at the top of the script select the resolution definition, the drift
term of the uncertainty budget and the figure style. Table numbers in the
output file follow an earlier draft of the paper; the `\label` names identify
each table unambiguously.
