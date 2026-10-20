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

**I2C static resources.** `c3i2c_init()` runs once in application startup,
before profile restore and USB requests. It makes the adapter's only kernel
creation call: one `TA_INHERIT` mutex, taken from the kernel's fixed mutex table
(`CNF_MAX_MTXID` 64), never the heap. The mutex is never deleted. Later init
calls create nothing. If creation fails, init and open keep failing until
reboot; Detect, Apply, rollback and recovery never retry it. Like the UART,
pulse and PWM startup calls, a failure stops `c3app_run` before USB opens. The
failure depends only on the build (for example a full kernel object table), so
it shows up as a boot-time FAIL on serial instead of a node that never runs I2C.
`c3i2c_open/close` only start and stop a session: pins, clock and controller.
The portable [`i2cown`](../../include/i2cown.h) module enforces the rules, and
[`owntest`](../../../../tests/target/owntest.c) counts every creation call:

- open: lifecycle-owner task, bus closed, pins free; polls the lock;
- close: lifecycle owner, no device-address claims, no transfer holding the
  lock (polled, never waited for);
- a refused pin release leaves the session *retained*: transfers and open are
  rejected until a retried close succeeds;
- each transfer rechecks the session after locking, so it cannot run after
  close;
- the bus callbacks are never cleared, so a preempted caller cannot reach a
  null pointer; a closed bus rejects use, and open refuses a stray claim.

Fixed RAM is static `.bss` of about 60 bytes on RV32: session settings 16,
borrowed bus 24 (including 16 claim bytes), lifecycle state 16, mutex handle 4.
The constant callback tables are in flash. There is one bus and one controller,
with no heap or queue.
`c3spi` now provides SPI2 FIFO transfers with configurable pins and a mutex.
`c3pin` provides a shared pin ledger and simple checked push-pull outputs.
I2C, SPI signals, chip selects and those outputs cannot claim overlapping pins.
Claims change only under the serialized lifecycle owner with affected users
stopped. See [SPI limits and binding](../../../drivers/ssd1306_spi/INTEGRATION.md).

The lifecycle owner opens/closes the I2C controller once device claims and
transfers are gone; SPI still requires no owned workers.
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
