# PET Filament Puller

Standalone Marlin controller for a RAMPS 1.4 / Arduino Mega 2560 machine that holds one hotend temperature and pulls filament with the E0 stepper. No host computer is required to run it.

This tree is [Marlin 2.1.2.10](https://github.com/MarlinFirmware/Marlin/releases/tag/2.1.2.10), commit `077b56cedf2bb4378077b52269b699da7849da5f`, plus the puller files below. Upstream Marlin is untouched except for four hooks. Machine settings live in one header.

The firmware compiled for `megaatmega2560`. It has not been run on the assembled machine. A successful compile does not mean the heater, thermistor, driver, or mechanics are safe.

## Confirm before power

Edit `Marlin/Marlin/pet_puller_config.h` before the first flash. These values are placeholders:

| Setting | Placeholder | What has to be true |
| --- | --- | --- |
| `PET_THERMISTOR_TYPE` | `1` (100k EPCOS table) | The number matches the sensor on T0. Table numbers are the list in `Configuration.h`. |
| `PET_E0_DRIVER_TYPE` | `A4988` | The type matches the chip on the E0 socket. Also set `PET_E0_DRIVER_LABEL` to the same name. |
| `PET_LCD_FULL_GRAPHIC` | selected | The panel on EXP1/EXP2 is a RepRapDiscount full-graphic 12864. |
| Supply vs cartridge | not in firmware | The RAMPS input voltage matches the heater cartridge. |
| Driver current | not in firmware | The module potentiometer and sense resistors are set for this motor. |

`PET_E0_DRIVER_TYPE` sets step timing. It does not set A4988/DRV8825 microstepping; the module jumpers do. TMC2208/TMC2209 boards used in step/dir mode need `TMC2208_STANDALONE` or `TMC2209_STANDALONE`. UART/SPI wiring is not configured.

LCD alternatives, exactly one of these defines:

- `PET_LCD_FULL_GRAPHIC` — RepRapDiscount Full Graphic Smart Controller (12864)
- `PET_LCD_SMART_20X4` — RepRapDiscount Smart Controller (20x4 character)
- `PET_LCD_CR10_STOCK` — Creality CR-10 stock display

Flash with the heater supply off. USB power is enough for the Mega and the thermistor. The first screen is **CONFIRM HARDWARE**. The displayed temperature has to look like the real room temperature. Leave **Not yet** selected if it does not. Confirming a wrong table can make a later setpoint dangerously wrong, including a table that happens to look right at room temperature. Confirmation stores a flag. It does not choose a safe process temperature, and it cannot see the supply voltage or the driver current.

The build prints this warning on purpose:

`PET Puller: confirm thermistor, E0 driver, LCD, and PSU voltage before powering the heater.`

## Hardware map

Board target: `BOARD_RAMPS_14_EFB` on ATmega2560. That FET order puts the hotend on D10.

| Function | RAMPS connection | Firmware pin |
| --- | --- | --- |
| Hotend cartridge | D10 MOSFET | `HEATER_0_PIN` 10 |
| Hotend thermistor | T0 | `TEMP_0_PIN` analog 13 |
| Pull stepper | E0 | STEP 26, DIR 28, ENABLE 24 (active low) |
| Panel | EXP1 / EXP2 | selected LCD controller |
| Fan MOSFET | D9 | unused; soft PWM so the beeper can use Timer 2 |
| Bed MOSFET | D8 | unused; bed sensor is disabled |
| X, Y, Z drivers | X, Y, Z | unused; disabled while idle |
| Endstops | X/Y/Z min/max | unused; no homing |
| USB serial | Mega USB | port 0, 250000 baud, optional |

There is no heated bed, no probe, no SD printing, and no X/Y/Z motion in normal use.

## Custom code

| File | Role |
| --- | --- |
| `Marlin/Marlin/pet_puller_config.h` | All machine settings. Included at the end of both configuration headers. |
| `Marlin/Marlin/src/feature/pet_puller.h` | Puller state machine. |
| `Marlin/Marlin/src/feature/pet_puller.cpp` | Motion, heater interlock, EEPROM record. |
| `Marlin/Marlin/src/lcd/menu/menu_pet_puller.cpp` | Status screen and PET PULLER menu. |
| `Marlin/ini/features.ini` | Compiles `pet_puller.cpp` when `PET_PULLER` is enabled. |

Hooks in upstream files: `Configuration.h`, `Configuration_adv.h`, `MarlinCore.cpp` (`init`, `tick`, `kill`, `stop`), `marlinui.cpp` (the status screen is the puller screen).

## Build and flash

PlatformIO environment `mega2560` (`board = megaatmega2560`).

```bash
cd Marlin
pio run -e mega2560
pio run -e mega2560 -t upload
```

The image is `Marlin/.pio/build/mega2560/firmware.hex`.

Last successful build of this tree:

- Flash: 90240 bytes of 253952 (35.5%)
- SRAM: 3337 bytes of 8192 (40.7%)
- Warnings: the confirmation `#warning` above, and no others

Use PlatformIO. Marlin 2.1 selects source files from `ini/features.ini`; the Arduino IDE build does not.

## Temperature

PID is Marlin's hotend PID. Stock gains shipped with Marlin are Kp 22.20, Ki 1.08, Kd 114.00. Those are generic sample values, not a tune for this hotend.

1. Set the operating temperature you intend to hold with **Set Temperature**. That number is your process choice. This firmware does not supply one.
2. Open **Settings**, set **Autotune C** to that same temperature, and run **Run PID autotune**. Stay with the machine. The menu sends `M303 U1 E0 S<temp>`.
3. Open **PID values** and check Kp, Ki, Kd.
4. Choose **Save settings**. Until that save, a reset restores the stock gains.

Marlin still enforces thermal runaway, heating-failed, mintemp, and maxtemp. Any of those calls `kill()`: heater off, motion discarded, firmware halted until a physical reset. **Clear Fault** cannot resume a kill. `HEATER_0_MAXTEMP` is 275, and the highest target the menu will accept is 260 because of `HOTEND_OVERSHOOT` 15. That is Marlin's stock ceiling, not a material limit.

Cold extrusion prevention stays on. `PET_EXTRUDE_MINTEMP` is 170 °C. Pulling is also refused below the runtime minimum.

With window `W`, hysteresis `H`, and minimum safe temperature `M`, a target `T` starts the motor at `min(T, max(T - W, M + H))` and stops it below `max(M, T - W - H)`. Defaults are W = 5 °C, H = 3 °C, M = 170 °C. Worked illustration only, for T = 200 °C: start at 195 °C, stop at 192 °C. **Start Pulling** is refused when the target is below `M + H`. The cold-extrusion check remains in force as well.

## Pulling speed

`mm/min` is filament distance along the roller, because volumetric extrusion is off. The compiled steps/mm is 100. That number is uncalibrated.

For a stepper with `S` full steps per revolution (200 for a typical 1.8° motor), microstep factor `M` (from the driver jumpers, not from this firmware), gear ratio `G` (motor revolutions per roller revolution; 1 for direct drive), and measured roller diameter `D` in millimetres:

```text
steps/mm = (S * M * G) / (pi * D)
```

Illustration of the formula only: 200 steps, 16 microsteps, direct drive, 10 mm diameter gives `3200 / (pi * 10) ≈ 101.9` steps/mm. Measure the roller. Do not use 10 mm unless that is the diameter.

Then check it. Set the calibrated estimate, run a known time at a known speed, and measure the filament length that actually moved:

```text
corrected steps/mm = current steps/mm * (commanded mm / measured mm)
```

Commanded mm is speed (mm/min) times minutes. Enter the result under **Settings → Steps/mm** while the motor is stopped, then **Save settings**. The menu accepts 1.0 to 9999.9 steps/mm in 0.1 steps. Acceleration is **Accel mm/s2** (default 100). E-only moves use that as retract acceleration.

The runtime speed cap starts at 300 mm/min and can be raised up to 1200 mm/min. The slowest non-zero speed is 5 mm/min, which stays above Marlin's minimum planner speed. While a pull is requested, the speed cannot be set to 0. **Stop Pulling** is what stops the motor.

**Motor Direction** reverses the sign of the extruder move and is stored. It can be changed while running: the queued segments finish, then the new direction starts.

## Operation

The knob changes pulling speed on the main screen, including while the motor is running. A click opens **PET PULLER**. While a pull is requested, **Stop Pulling** is the first action, so the next click stops.

```text
PET PULLER
Temperature:  actual/target
Speed:        mm/min
Motor:        STOPPED | HEATING | READY | RUNNING | STOPPING | FAULT
Start Pulling
Stop Pulling
Set Temperature
Set Pulling Speed
Motor Direction
Heater Off
Settings
```

`STOPPED` is the idle state. `READY` means the nozzle is hot and the motor is not pulling. `STOPPING` is the deceleration after Stop.

States:

- **IDLE** — heater target 0, motor off. Shown as STOPPED.
- **HEATING** — target is set, or a pull is waiting for the start temperature.
- **READY** — at temperature, pull not requested.
- **RUNNING** — the planner is being kept filled with short E0 segments.
- **STOPPING** — no new segments; the last segments decelerate to a stop.
- **FAULT** — latched. Heater off, motion discarded, motor disabled.

**Start Pulling** copies the saved preset onto the live target if the heater is off, then waits until the nozzle is hot enough. **Heater Off** clears the live target and stops the pull. The preset remains so the next Start can use it.

A temperature dip below the stop threshold while running decelerates the motor, shows HEATING, and continues the same Start request when the nozzle is hot again. A reset, a power loss, or a fault does not do that. Boot always forces the live target to 0 and leaves the motor off, even when EEPROM holds a preset and a speed. The next pull starts only from **Start Pulling**.

**Settings** holds the minimum safe temperature, window, hysteresis, maximum speed, steps/mm, acceleration, PID autotune, the hardware lock, and **Save settings**. Steps/mm and acceleration can be edited only while the motor is fully stopped.

Serial is optional. `M112` still stops the machine immediately (emergency parser). Do not send `G0`/`G1` while a pull is queued; the puller owns E0. Temperature and EEPROM commands (`M105`, `M104`, `M303`, `M301`, `M500`) remain available for service.

## What is stored

Marlin's own EEPROM block (PID and the usual motion settings) starts at address 100. The puller record starts at address 3584 (`PET_EEPROM_BASE`), with magic `PET1`, version 1, and a CRC. If Marlin's block would overlap that address, the puller keeps the compiled defaults and says so on serial.

The record stores the preset temperature, speed, maximum speed, minimum safe temperature, window, hysteresis, autotune temperature, steps/mm, acceleration, direction, and the hardware-confirmation flag. It does not store running/heating state.

Speed and limit edits are written about 5 seconds after the last change. **Save settings** writes the puller record and Marlin's EEPROM (use this after PID autotune). **Confirm hardware** and **Lock heater** write immediately.

## Motion design

The puller queues at most three extrude-only planner blocks, each about 200 ms of travel and between 0.5 mm and 5 mm. `buffer_line` is called only when the planner already has a free slot, so the call does not wait. The stepper ISR, temperature ISR, LCD, and watchdog stay on Marlin's normal paths. There is no step-pin bit bang and no repeated G-code injection.

After each queued block the logical E position is put back, so a long pull does not walk the planner coordinate off the end of a 32-bit count. The stepper ISR consumes each block's own step delta. The absolute step counter is re-synced when the motor has fully stopped.

The queue is deliberately shallow, so Marlin's print-buffer slowdown is disabled. The extruder is not dropped between segments (`DEFAULT_STEPPER_TIMEOUT_SEC` is 0). It is disabled once the queue is empty.

## Safety behavior

- Boot, reset, and power loss: heater off, steppers disabled, no automatic pull.
- Hardware not confirmed: heater target forced to 0. Start opens the confirm screen.
- Below the start temperature, or below the cold-extrusion floor: the motor is not started.
- Temperature falls through the stop threshold during a pull: smooth stop, then heat, then resume only for that same Start request.
- Thermistor open, short, thermal runaway, or heating failure: Marlin `kill()`. Heater off, motor discarded, halt until reset. The following boot does not resume.
- **Stop Pulling** decelerates through the planner. A fault uses `quick_stop` and disables the driver.
- The watchdog, cold-extrusion check, lengthy-extrude check, and hotend thermal protection are still compiled in.

Stay with the machine for the first heat, the first PID autotune, and the first pull. Confirm the sensor table, the driver, the microstep jumpers, the supply voltage, and the mechanical speed before leaving it to run.
