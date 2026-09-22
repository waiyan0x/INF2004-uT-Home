# Configuration profiles

This directory contains the versioned JSON inputs consumed by the μT-Home
configuration manager. The first prototype uses one ESP32-S3 node profile with
four peripherals so that I²C, SPI, UART, and GPIO can run under the same
firmware image.

## Layout

- `nodes/first-prototype.json` is the runtime profile loaded by the prototype.
- `scenarios/first-prototype.json` describes the end-to-end demonstration and
  the observable results used by integration tests.
- `invalid/` contains profiles that a validator must reject. They document
  required semantic checks before the validator is implemented.

All JSON files use UTF-8, two-space indentation, and a `schema_version`. Device
IDs, interface IDs, task names, and queue names must be unique within a node.
GPIO numbers refer to ESP32-S3 GPIO numbers rather than physical header
positions.

## Prototype pin assignment

| Function | GPIO | Notes |
|---|---:|---|
| BMP388 I²C SDA | 8 | Shared I²C bus `i2c0` |
| BMP388 I²C SCL | 9 | Shared I²C bus `i2c0` |
| OLED SPI CS | 10 | Device-specific chip select |
| OLED SPI MOSI | 11 | SPI bus `spi2` |
| OLED SPI SCLK | 12 | SPI bus `spi2` |
| OLED D/C | 13 | Data/command select |
| OLED reset | 14 | Active-low reset |
| LD2450 UART RX | 16 | Connect to LD2450 TX |
| LD2450 UART TX | 17 | Connect to LD2450 RX |
| HC-SR04 trigger | 4 | ESP32-S3 output |
| HC-SR04 echo | 5 | ESP32-S3 input through a 5 V-to-3.3 V divider |

The pin assignment is the software baseline for prototype wiring. Confirm it
against the exact ESP32-S3 SuperMini board pinout before power is applied.

## Loading the profile

The configuration manager must parse and validate the complete profile before
starting any device task. If parsing, schema validation, pin allocation, or
resource checks fail, it must retain the last-known-good configuration and
report the error through system health monitoring.
