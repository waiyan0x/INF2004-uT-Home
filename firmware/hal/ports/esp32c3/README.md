# ESP32-C3 SuperMini HAL port

This directory owns team-written ESP32-C3 board access: peripheral handles,
GPIO configuration, interrupt glue, monotonic time, persistent configuration,
and USB serial transport. Use the SuperMini board kit from the pinned
`../../../third_party/esp32c3-mtk3/` dependency; see
[the setup guide](../../../README.md#pinned-target).

Load the resource map from the validated node profile and board capabilities.
An example allocation and module checks are in
[configs/README.md](../../../../configs/README.md).
GPIO18/19 belong to native USB-Serial-JTAG; GPIO12-17 are excluded for flash.
The broader board profile conditionally permits GPIO2 as active-low chip select
with a 10 kOhm pull-up. The current backend excludes strapping pins, including
GPIO2; target binding must reject that choice until explicitly qualified.

The pinned port registers `DEV_SER` on UART1, TX21/RX20. `c3uart` now wraps that
driver with exclusive ownership and bounded polling. Use the application resource patch
for a 512-byte usable ring and disabled unused ADC startup. Configure baud,
framing and finite waits from the validated instance; the LD2450 example uses
256000 baud and 8N1. Do not install a second UART1 driver.
The wrapper queries available bytes/space before I/O and reopens once to apply
the native driver's cached settings. See [UART binding and cleanup limits](../../../drivers/hlk_ld2450/INTEGRATION.md).
The upstream concurrent read/write limitation requires single ownership.
The port has no native μT-Kernel I2C device-manager driver. The project now
provides `c3i2c` through the pinned SDK's low-level hardware interface. It
supports bounded write/read/repeated-START transactions, one inherited-priority
kernel mutex and a common absolute deadline for lock waiting and transfer.
`c3spi` now provides SPI2 FIFO transfers with configurable pins and a mutex.
`c3pin` provides a shared pin ledger and simple checked push-pull outputs.
I2C, SPI signals, chip selects and those outputs cannot claim overlapping pins.
Claims change only under the serialized lifecycle owner with affected users
stopped. See [SPI limits and binding](../../../drivers/ssd1306_spi/INTEGRATION.md).

The lifecycle owner opens/closes the controller with no workers owned.
Configured pins/clock stay in the backend; device addresses stay in requests.
GPIO0/1/3/4/5/6/7/10 are accepted. UART20/21 remain unavailable while the selected
native startup owns them; USB/flash/strapping pins are excluded. External pull-ups
must be confirmed for the chosen wiring and clock. Internal pull-ups are only
supplementary and do not qualify signal rise times.

The initial FIFO implementation accepts at most 30 write and 32 read bytes.
Combined operations use repeated START and a final NACK/STOP. Interrupts remain
disabled; polling reads raw status because the SDK's `get_intr_mask` helper
reads masked status. Short polls yield to the kernel after 25 attempts. Timeout,
NACK and arbitration errors reset controller state without an unbounded retry.
Stuck external lines still require correcting the wiring/device; this is not a
general bus-unwedge service.

Compilation is verified; physical I2C/SPI/output tests are deferred to final
hardware acceptance. The five LL-based C3 adapters use C99 syntax; compilation needs
GNU99 because pinned external LL headers use GNU `typeof`. Other ordinary owned
modules compile as C99.

No driver or service may directly include ESP32-C3 SDK headers. Kernel-specific
worker calls belong in the runtime adapter. HAL synchronization uses the pinned
kernel mutex API and the runtime operating-time clock; drivers include neither
kernel nor SDK headers.

## Pulse capture and PWM software

`c3pin` now provides owned digital input as well as output. `c3pulse_port(0..1)`
borrows two RMT capture pairs; `c3pwm_port(0..3)` borrows four independently timed
LEDC outputs. The factories have no I/O; lifecycle setup uses the shared pin
ledger. See [timing, ownership and deferred hardware acceptance](../../../drivers/hc_sr04/INTEGRATION.md).
These two backends also use GNU99 solely for excluded SDK LL header syntax.
No GPIO/actuator capability is advertised until the relevant acceptance passes.
