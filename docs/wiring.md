# Wiring

## Rig controller (Arduino Nano ESP32 on PCB V2.1 / V2.2)

Pin names as defined in `firmware/rig/Force_Control_Rig_V4/Force_Control_Rig_V4.ino`.

| Function | Pin | Net | Notes |
|---|---|---|---|
| ADS1220 chip select | D10 | CS_ADS1220 | SPI (D11 MOSI, D12 MISO, D13 SCK) |
| ADS1220 data ready | D9 | DRDY | |
| Motor PWM | D6 | PWM | to Pololu G2, 20 kHz |
| Motor direction | D5 | DIR | to Pololu G2 |
| Driver fault | A2 | MOTOR_FAULT | open-drain, LOW = fault |
| Motor current | A0 | I_SENSE | ACS773 output via 1 kΩ / 100 nF |
| Driver temperature | A3 | THERMISTOR | 10 kΩ to 3V3, NTC to GND |
| E-stop sense | A1 | EMER_SENSE | HIGH = pressed |
| Limit, push end | D7 | LS1 | |
| Limit, retract end | D8 | LS2 | |
| Buzzer | D4 | Buzzer+ | LEDC PWM |
| Status LED red / green | D2 / D3 | LEDR / LEDG | 220 Ω |
| UART to touchscreen | D1 (TX) / D0 (RX) | | 115200 baud |

## Load cell → ADS1220 (4-wire, ratiometric)

| Load-cell wire | Function | PCB |
|---|---|---|
| Excitation + | E+ | bridge excitation; also REFP0 |
| Excitation − | E− | bridge excitation; also REFN0 |
| Signal + | S+ | AIN+ |
| Signal − | S− | AIN− |
| Shield | — | GND at the PCB end only |

Wire colours vary between manufacturers: use the cell's datasheet.

## Touchscreen link

Cross-wired UART with a common ground:

| Rig (Nano ESP32) | Touchscreen (ESP32-S3) |
|---|---|
| D1 (TX) | GPIO 18 (RX) |
| D0 (RX) | GPIO 17 (TX) |
| GND | GND |

On the PCB this link leaves through **J2** (PicoBlade, silkscreen "LCD UART",
RX IO18 / TX IO17). **PCB V2.1** (as built) carries the two UART lines on J2
pins 3–4; **V2.2** moves them to pins 1–2. Make the touchscreen cable to match
your board revision and check it against the schematic.

Do **not** use GPIO 43/44 on the touchscreen: they carry the ESP32-S3 boot log,
which the rig would read as commands.

## Power and e-stop

- 12 V DC supply → PCB power input → Pololu G2 motor supply.
- The latching e-stop removes drive to the actuator and is sensed on A1, so the
  firmware latches a fault and refuses to restart until it is released and reset.
- Logic (Nano ESP32, touchscreen) is powered from the PCB's regulated rail.
