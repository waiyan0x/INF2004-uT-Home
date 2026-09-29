# Peripheral drivers

Each driver uses only contracts from `firmware/hal/include/` and publishes or
consumes the hardware-independent types declared under `include/uthome/`.

The first prototype contains:

| Directory | Protocol | Responsibility |
|---|---|---|
| `bmp388/` | I²C | Pressure and temperature acquisition |
| `ssd1306_spi/` | SPI + GPIO | Provisional 128×64 OLED transport |
| `hlk_ld2450/` | UART | Framing and target-report decoding |
| `hc_sr04/` | GPIO + time | Trigger/Echo distance measurement |

Drivers do not create tasks, allocate queues, choose pins, format the complete
OLED view, or call another driver. They expose initialise, operate, and shutdown
entry points through the component registry and return common status codes.

`hc_sr04` now includes portable measurement and `hcinst` typed publication.
`pwm_output/pwminst` is an additional generic output component for the wider
platform, independent of the first prototype. Neither is hardware-qualified.
See [GPIO integration](hc_sr04/INTEGRATION.md).
