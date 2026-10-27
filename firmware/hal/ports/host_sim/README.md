# Host simulation HAL

`i2cfake.c` implements the I2C/time contract with two register fixtures,
configurable transfer cost, required delays, disconnects and a selected failing
transfer. Analytic calibration data allows exact compensation assertions.

It is single-threaded and models no electrical timing or physical accuracy.
`spifake.c` captures bounded transfers, CS/mode/clock and optional D/C levels.
It injects attach/detach, line and transfer failures with a common fake clock.
Its full-duplex echo is a software fixture, not an electrical loopback result.
`gpiofake` provides virtual pulse/PWM state and fault injection. UART behavior
is described below. Storage fault fixtures live with configuration tests rather
than this HAL port; none proves electrical timing or physical persistence.

The fake APIs must implement the same contracts and timeout behavior as the
ESP32-C3 port.

`uartfake` models independent 512-byte receive/transmit buffers, short writes,
explicit time, overflow and injected setup/read/flush/cleanup failures. It is
used with the actual streaming parser, driver and activation adapter; it does
not simulate native kernel UART interrupts or physical timing.
