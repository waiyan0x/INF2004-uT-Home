# Configuration profiles

This directory contains the versioned JSON inputs consumed by the μT-Home
configuration manager. Node schemas 2.0.0/2.1.0 reference reviewed device descriptors
and board capabilities. The mixed first prototype, two-environment-sensors and
distance-only profiles demonstrate different arrangements under one runtime.
See [PROJECT_BASELINE.md](../PROJECT_BASELINE.md) for the platform contract.

## Layout

- `nodes/first-prototype.json` is one example deployment.
- `nodes/two-environment-sensors.json` uses independent instances of one driver.
- `nodes/distance-only.json` has no display or other sensor dependency.
- `nodes/distance-controlled-output.json` adds a generic rule and PWM output
  under schema 2.1.0. Its example wiring is not physically qualified.
- `builds/` distinguishes host admission fixtures from target driver availability.
- `PROTOTYPE_REFERENCE.md` preserves superseded baseline 1.0.0 notes.
- `scenarios/first-prototype.json` describes the end-to-end demonstration and
  the observable results used by integration tests.
- `invalid/` contains profiles that a validator must reject. They document
  semantic checks implemented by `tools/config_validator`.

All JSON files use UTF-8, two-space indentation, and a `schema_version`. Device
IDs, interface IDs, task names, and queue names must be unique within a node.
GPIO numbers refer to ESP32-C3 GPIO numbers rather than physical header
positions.

## Example C3 SuperMini pin assignment

| Function | GPIO | Notes |
|---|---:|---|
| BMP388 I²C SDA | 0 | Shared I²C bus `i2c0` |
| BMP388 I²C SCL | 1 | Shared I²C bus `i2c0` |
| OLED SPI CS | 2 | Active-low CS; boot-strapping check required below |
| OLED SPI MOSI | 6 | SPI bus `spi2` |
| OLED SPI SCLK | 7 | SPI bus `spi2` |
| OLED D/C | 3 | Data/command select |
| OLED reset | 10 | Active-low reset |
| LD2450 UART RX | 20 | UART1 RX; connect to LD2450 TX |
| LD2450 UART TX | 21 | UART1 TX; connect to LD2450 RX |
| HC-SR04 trigger | 4 | ESP32-C3 output |
| HC-SR04 echo | 5 | ESP32-C3 input through a 5 V-to-3.3 V divider |

This allocation belongs to the first-prototype example; other valid allocations
are admitted by the board capability file. Physical wiring is not yet tested.
See [the catalogue](../catalogue/README.md) for device and board limits. The old S3 assignment
used pins unavailable or reserved on this board.

- Keep GPIO18/19 for native USB-Serial-JTAG, including configuration and console.
- Leave GPIO8/9 unused by peripherals to avoid board LED/BOOT interactions.
- Exclude GPIO11 (not on the pictured headers) and GPIO12-17 (flash signals).
- GPIO2 is a strapping pin. Fit a 10 kOhm pull-up to 3.3 V and check the OLED
  does not pull CS low through reset. Verify boot and download mode with the
  display connected.
- UART1 uses GPIO20/21 while the console uses native USB. The port's `DEV_SER`
  owns it and our HAL wraps that driver, with a single reader/command task.
  Apply the tracked native-ring patch during integration; do not install a
  competing UART driver.
- HC-SR04 Echo divider: 10 kOhm from Echo to GPIO5 and 15 kOhm from GPIO5 to
  ground, nominally 3.0 V at a 5.0 V Echo. Check the actual level.

GPIO constraints are documented in the [Espressif C3 GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html).
The actual flash size must be detected on hardware; the upstream baseline
expects 4 MB. Do not infer it from the board photographs.

## Loading the profile

The configuration manager must parse and validate the complete profile before
starting any device task. If parsing, schema validation, pin allocation, or
resource checks fail, it must retain the last-known-good configuration and
report the error through system health monitoring.
