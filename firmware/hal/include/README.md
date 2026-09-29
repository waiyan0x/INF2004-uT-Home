# HAL contracts

`i2cbus.h` implements bounded writes, reads and repeated-START transfers,
absolute deadlines, a required clock/delay and lifecycle address claims.
The initial FIFO path accepts at most 30 transmitted and 32 received bytes.
Drivers borrow backend-owned bus storage until checked cleanup succeeds.

`spibus.h` adds bounded SPI transfers, distinct attached devices and explicit
driver ownership. `pinpool.h` supplies all-or-nothing pin claims; `digiout.h`
defines exclusive checked output callbacks. See the
[SPI integration guide](../../drivers/ssd1306_spi/INTEGRATION.md).

Remaining public HAL headers will define bounded operations for:

- UART initialisation and bounded frame reception;
- GPIO input, edge timing, pulse generation and PWM;
- monotonic time, delays, configuration storage, and USB serial transport.

Each operation returns a common status code and accepts an explicit timeout
where blocking is possible. Driver code must depend on these contracts rather
than ESP-IDF directly.

`uartio.h` defines exclusive UART ownership and bounded RX/TX. It contains no
kernel types or radar assumptions. Backends may reject unsupported controller,
pin, framing, speed or compiled buffer-capacity requests.
