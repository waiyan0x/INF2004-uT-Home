# HAL contracts

Public HAL headers will define bounded, timeout-aware operations for:

- I²C bus initialisation and register transactions;
- SPI bus initialisation and device transactions;
- UART initialisation and bounded frame reception;
- GPIO input, output, edge timing, and pulse generation;
- monotonic time, delays, configuration storage, and USB serial transport.

Each operation returns a common status code and accepts an explicit timeout
where blocking is possible. Driver code must depend on these contracts rather
than ESP-IDF directly.
