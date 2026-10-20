# Hardware abstraction layer

`include/i2cbus.h` defines the implemented I2C transport and timing contract.
One absolute deadline covers each complete operation, including bus-lock waiting.
The backend owns the controller while drivers claim their device addresses.
`include/i2cown.h` separates the permanent bus lock, created once at startup,
from repeatable open/close sessions; see the
[C3 port](ports/esp32c3/README.md) for its limits.
`include/spibus.h` adds bounded SPI device transactions, and `include/spiown.h`
applies the same permanent-lock/session split to SPI; `pinpool` tracks pin
ownership and `digiout.h` defines borrowed output callbacks. C3 I2C, SPI and simple
outputs share one pin ledger. UART, digital input, pulse capture and PWM now
have software implementations. Storage and USB configuration transfer live in
[configuration](../src/config/README.md); full hardware acceptance remains open.

Peripheral HAL backends live in `ports/esp32c3/`; USB/storage adapters live in
`src/config/`. They select pins from validated profiles. Device drivers remain
independent of board pin assignments and SDK APIs.

`ports/host_sim/i2cfake.c` provides a deterministic I2C register/time fake;
`spifake.c` captures SPI transactions and injects output/transport failures.
Adding a board means adding another port without changing drivers or services.

Network APIs are deferred because the first prototype uses USB serial for
configuration and monitoring.

## UART

`uartio` owns one configurable byte transport with 64-byte nonblocking calls,
partial TX counts, checked cleanup and monotonic time. `uartfake` supplies host
faults; `c3uart` wraps the pinned kernel UART1. Protocol parsing remains in the
driver. See [UART integration](../drivers/hlk_ld2450/INTEGRATION.md).
Native UART hardware qualification remains pending.

## GPIO capture and PWM

`digiin` complements `digiout` for borrowed level callbacks. `pulseio` owns a
trigger/input pair and returns bounded pulse widths; `pwmio` owns a PWM channel
with zero-duty setup/cleanup. `gpiofake` provides host faults and shared pin
claims. `c3pulse` uses RMT; `c3pwm` uses independently timed LEDC outputs.
See [limits and review](../drivers/hc_sr04/INTEGRATION.md). Physical acceptance
and generic GPIO interrupt delivery remain pending.
