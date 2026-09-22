# Host simulation HAL

The host port supplies deterministic fakes for configuration, time, I²C, SPI,
UART, and GPIO. Tests can inject samples, partial UART frames, timeouts,
disconnections, and pin-allocation failures without target hardware.

The fake APIs must implement the same contracts and timeout behavior as the
ESP32-S3 port.
