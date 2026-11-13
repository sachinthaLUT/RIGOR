# Serial protocol

The rig accepts single-letter commands, one per line, on both the USB serial
port and the touchscreen UART (115200 baud, `\n` terminated). The USB console
echoes each command and prints human-readable replies; send `?` for the list.

## Commands

| Command | Meaning |
|---|---|
| `H<kg>` | Seek and hold a target force, e.g. `H30` (0–90 kg) |
| `S` | Stop the run (motor off; the leadscrew holds position) |
| `Z` | Clear a latched fault |
| `R` | Report force, current, temperature and e-stop state |
| `B` | Stop, then retract a short distance |
| `+` / `-` | Jog forward / back about 1 mm (open loop) |
| `O1` / `O0` | Tare now (idle, \|F\| ≤ 2 kg, 0.5 s average) / clear the tare |
| `V0` / `V1` / `V2` | Buzzer off / low / high |
| `F<hz>` | Telemetry rate to the touchscreen, 1–50 Hz |
| `W0` / `W1` | On touchscreen link loss during a run: hold / retract |
| `E0` / `E1` | After an e-stop reset: clear only / retract |
| `Q` | Short beep |
| `P` | Heartbeat (silent); the touchscreen sends it continuously |
| `?` | Help |

## Telemetry (rig → touchscreen)

One line per telemetry period:

```
T F=<kg> tgt=<kg> pwm=<signed duty> reg=<APPR|CONT> st=<state> lim=<bits>
  I=<A> Ipk=<A> Td=<°C> es=<0|1> df=<0|1> eot=<bits> N=<counts> tare=<kg> [flt=<reason>]
```

| Field | Meaning |
|---|---|
| `F` | Measured force, tared (kg) |
| `tgt` | Current target (kg) |
| `pwm` | Applied duty, signed (0–255 scale; + push) |
| `reg` | `APPR` approaching contact, `CONT` in contact |
| `st` | Rig state; `FAULT` when latched |
| `lim`, `eot` | Bit 0 push end, bit 1 retract end |
| `I`, `Ipk` | Motor current, peak current (A) |
| `Td` | Driver temperature (°C; −999 if the sensor is absent) |
| `es`, `df` | E-stop pressed, driver fault |
| `N` | Raw ADS1220 counts (mean over one 20 ms tick, never tared) |
| `tare` | Active tare offset (kg) |
| `flt` | Fault reason, present only in `FAULT`, always last |

If no command or heartbeat arrives for 5 s during a run, the rig stops on its
own and, if `W1` is set, retracts to unload the specimen.
