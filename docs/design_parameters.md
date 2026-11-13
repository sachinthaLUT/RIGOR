# Design parameters and scaling

| Parameter | As built | Selection guidance |
|---|---|---|
| Force range | 0–90 kg (≈ 883 N) controlled, software cap; actuator 1500 N | ≥ 1.5 × the maximum in-service load of the device under test |
| Load-cell capacity | 100 kg full scale | Match the peak applied force; stay below the cell's safe overload (150 % FS) |
| Stroke | 100 mm | Largest specimen displacement plus fixturing clearance |
| Travel speed | 5 mm/s | Sets the dynamic loading rate; faster actuators trade force and control resolution |
| Static force accuracy | ±0.3 % FS | Dominated by the load cell grade, not the ADC |
| Mechanical bandwidth | ~1–2 Hz | Limited by actuator speed and compliance |
| Control loop rate | 50 Hz | One mains period per sample rejects 50 Hz pickup; use 60 Hz-compatible timing where needed |
| Duty cycle | 25 % | Respect the actuator rating; add rest periods on cyclic tests |
| Supply | 12 V DC; 5 V / 3.3 V logic | Budget for actuator stall current |
