# Hardware abstraction layer

`include/` defines GPIO, I²C, SPI, UART, time, storage, and USB serial contracts.
All potentially blocking functions accept a timeout and return a common status.

`ports/esp32s3/` is the only location for ESP32-S3 SDK or board-specific code.
It configures peripherals from the validated profile; pin numbers must never be
compiled into a driver.

`ports/host_sim/` provides deterministic fakes for unit and integration tests.
Adding a board means adding another port without changing drivers or services.

Network APIs are deferred because the first prototype uses USB serial for
configuration and monitoring.
