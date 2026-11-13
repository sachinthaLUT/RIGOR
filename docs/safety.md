# Safety

RIGOR is a powered machine. A loaded specimen stores energy and can fail
suddenly. Use it only after reading this page.

## Hazards

- **Pinch and crush** between tup and reaction plate. Never place fingers in
  the load line while the rig is powered.
- **Specimen failure.** Brittle specimens can fragment; use a guard or shield
  and eye protection.
- **Stored load.** The leadscrew is self-locking: after a stop or power loss the
  force stays on the specimen. Retract before removing it.
- **Electrical.** 12 V DC at several amps; fuse the supply and keep the enclosure closed.

## Protective functions

| Function | Action |
|---|---|
| Emergency stop (latching) | Removes drive, latched fault, reset needed |
| Overload (95 kg) | Latched stop |
| Overcurrent | Latched stop after 0.5 s above threshold |
| Driver over-temperature / driver fault | Latched stop |
| End of travel (limit switches) | Motion inhibited in that direction |
| Approach timeout | Latched stop if contact is never made |
| Touchscreen link watchdog (5 s) | Stop, optionally retract |
| Set-point cap (90 kg) | Higher commands refused |

The software force limit is the main protection for the load cell, because the
actuator can exceed the cell's rating. Do not raise `OVERLOAD_TRIP_KG` above
the cell's safe overload.

## Before every session

- E-stop works (press it during a slow jog).
- Load path aligned and all fasteners tight.
- Unloaded reading near zero; tare if needed.
