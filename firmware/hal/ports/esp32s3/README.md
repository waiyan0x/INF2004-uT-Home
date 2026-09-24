# ESP32-S3 HAL port

This port maps the public HAL to the selected ESP32-S3 toolchain and board. It
owns pin configuration, peripheral handles, interrupt glue, monotonic timing,
non-volatile configuration storage, and USB serial transport.

The first-prototype resource map is loaded from configuration:

- I²C0: GPIO8 SDA, GPIO9 SCL;
- SPI2: GPIO11 MOSI, GPIO12 SCLK;
- UART1: GPIO16 RX, GPIO17 TX;
- OLED control: GPIO10 CS, GPIO13 D/C, GPIO14 reset;
- HC-SR04: GPIO4 Trigger, GPIO5 Echo.

No driver or service may include ESP32-S3-specific headers directly.

## I²C and SPI port requirements

The contracts in `hal/include/i2c.h` and `spi.h` are only declarations; this
port must enforce the same rules the host simulation does, so tests on the host
predict behaviour on the board:

- `hal_i2c_init` / `hal_spi_init` return `HAL_ERR_INVALID_ARG` for a zero
  `clock_hz` or a port/host that is already initialised.
- `hal_spi_device_add` returns `HAL_ERR_INVALID_ARG` for a mode above 3 or a
  `cs_gpio` already used as another device's chip select.
- `hal_spi_deinit` removes any devices still attached; transfers through their
  old handles return `HAL_ERR_NOT_INIT`.
- `hal_spi_transfer` with a non-NULL `rx` on a bus whose `miso_gpio` is
  `HAL_GPIO_NONE` returns `HAL_ERR_UNSUPPORTED`.
- A timeout and a NACK must come back as `HAL_ERR_TIMEOUT` and `HAL_ERR_NACK`,
  never folded into `HAL_ERR_BUS`. The BMP388 driver passes them straight to
  the health path, and the disconnect scenario depends on it.
- Each bus has a lock (a μT-Kernel semaphore or mutex) held for the whole of
  one transaction, so tasks sharing `i2c0` or `spi2` cannot interleave on the
  wire. init, deinit, device add and device remove run from one task only.
- The BMP388 needs a real `delay_us` in its config for soft reset and mode
  changes; the host tests pass `NULL`.
