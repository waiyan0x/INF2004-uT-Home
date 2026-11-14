# ESP32-C3 SuperMini HAL port

This directory owns team-written ESP32-C3 board access: peripheral handles,
GPIO configuration, interrupt glue, monotonic time, persistent configuration,
and USB serial transport. Use the SuperMini board kit from the pinned
`../../../third_party/esp32c3-mtk3/` dependency; see
[the setup guide](../../../README.md#pinned-target).

Experimental SDK network adapters and radio diagnostics are grouped in
[`network/`](network/README.md), separate from the peripheral and storage ports.

Board-specific configuration partition checks and bounded slot access live in
[`storage/`](storage/README.md), beneath the SDK-facing `c3store` adapter.

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
The wrapper queries available bytes/space before I/O and retains the boot-opened
native descriptor. Reconfiguration applies bounded STOP/reset/START handling
without a native reopen. See [UART binding and cleanup limits](../../../drivers/hlk_ld2450/INTEGRATION.md).
The upstream concurrent read/write limitation requires single ownership.
UART, pulse-capture and PWM close operations accept the runtime's explicit
cleanup-owner context after worker dormancy. They must not use ordinary
`rt_port_is_owner` alone there: that predicate intentionally rejects lifecycle
callback re-entry. Open operations retain the ordinary owner check. See the
[runtime contract and manual review](../../../src/runtime/README.md).
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

**SPI static resources.** `c3spi_init()` follows the same rule, right after
`c3i2c_init()`: one `TA_INHERIT` mutex from the fixed table, never deleted, so
the two buses hold two of the 64 mutexes. A failure stops `c3app_run` before
USB opens. `c3spi_open/close` only start and stop a session, enforced by the
portable [`spiown`](../../include/spiown.h) module and counted by
[`spowntest`](../../../../tests/target/spowntest.c):

- open: lifecycle-owner task, bus closed, pins free, no stray attached device;
  polls the lock;
- close: lifecycle owner, no attached device (the OLED chip select), no
  transfer holding the lock (polled, never waited for);
- a refused pin release leaves the session *retained*: transfers, attach and
  open are rejected until a retried close succeeds;
- each transfer rechecks the session after locking; a refused transfer never
  touches a chip select or SPI2 register;
- attach/detach require an open session but not the owner check, because the
  OLED's detach runs inside worker cleanup, where that check reports false;
- the bus callbacks are never cleared.

Fixed RAM is static `.bss` of about 60 bytes on RV32: session settings 12,
borrowed bus 28 (four device slots and chip selects), lifecycle state 16,
mutex handle 4. Limits are unchanged: four devices, 1024 bytes per transfer in
64-byte FIFO chunks, no DMA, heap or queue.

The lifecycle owner opens/closes both controllers once device claims or
attached devices and transfers are gone; neither checks a worker count.
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

Basic I2C/SPI operation has scoped bench evidence. Broader bus, output and
combined-system acceptance remain open; consult the qualification index.
The five LL boundaries (`c3ihw`, `c3shw`, `c3pin`, `c3pulse`, `c3pwm`) use C99 syntax; compilation needs
GNU99 because pinned external LL headers use GNU `typeof`. Other ordinary owned
modules compile as C99.

### Transfer fault boundary and review

`c3i2c` and `c3spi` retain transfer sequencing, deadlines, retry eligibility,
locking and reset decisions. Their nonpolling native register operations live
in `c3ihw` and `c3shw`; `c3bwait` contains the kernel waits and crystal clock.
The hardware modules retain the SDK's volatile MMIO accessors and exact register
ordering. No extra retained bus state, task, mutex, queue or allocation is added.

`test_bus_recovery.py` compiles the real transfer adapters and portable bus,
ownership and pin-ledger modules as C99 with warnings as errors. Object-level
aliases substitute owned interfaces, leaving vendor declarations untouched.
Twenty-one fresh-process scenarios cover I2C NACK/arbitration, hardware and
deadline timeouts, clock/yield failures, held-SCL failure/success, short FIFO,
one-byte/final-NACK and full-FIFO reads; SPI covers configuration/update/transfer
stalls, partial second-chunk failure, clock/yield/lock faults and a three-chunk
CS window. Every failure is followed by a successful transfer and clean close.

Manual BARR-C review covered rules 1-8: paired headers and ordinary public names;
unchanged permanent handle and session layouts; native ID is pinned signed int;
finite waits fit signed TMO; command/FIFO counts are bounded before narrowing;
the saved timeout structure is automatic, with the same three fields as before;
no callback cast, heap, pointer lifetime extension or concurrency relaxation.
Polling loops retain caller deadlines and kernel yields. Tests keep assertions,
verify untouched receive tails, and bound clock calls to expose infinite loops.
Host faults establish software recovery contracts; physical waveforms, scheduler
timing and combined hardware acceptance still require the bench procedure.

Fresh software evidence on 2 November 2026: all 21 fault scenarios and 38 target
qualification checks passed; the full pinned C gate checked 323 files. The
configuration-app native build in
`%TEMP%/uthome-desloppify-bus-boundaries-20261102` passed with a 231040-byte image
(432 bytes above the preceding service/storage build). Its ELF retains the
12832-byte binding, 16464-byte controller and four-byte bus mutex handles.
Local logs are `build/desloppify-bus-recovery-r3.log`,
`build/desloppify-bus-qualification-r1.log` and
`build/desloppify-bus-native-r1.log`. No board was flashed or exercised.

No driver or service may directly include ESP32-C3 SDK headers. Kernel-specific
worker calls belong in the runtime adapter. HAL synchronization uses the pinned
kernel mutex API and the runtime operating-time clock; drivers include neither
kernel nor SDK headers.

### Integration with local cleanup fixes

The 26 October `system-integration` update retains the startup-only resources
while preserving the local `rt_port_is_cleanup_owner()` permission in UART,
pulse and PWM close callbacks. The UART conflict resolution retains its new
`b_ready` and poison checks. Only the lifecycle owner's guarded cleanup context
receives this permission; it does not permit initialization/open or another
task to claim resources. The UART descriptor/mutex are still never deleted by
close, and failed cleanup retains logical ownership.

Manual BARR-C review of this resolution covered rules 1-8: no API, ABI, cast,
storage, loop or language-profile change; Boolean operands are explicitly
grouped, existing initialization/error checks remain, and synchronization and
cleanup bounds are unchanged. Existing cleanup-owner/shared-guard tests were
retained alongside the team's lifecycle tests. This is a source review, not
new wire-level or board-execution evidence.

## Pulse capture and PWM software

`c3pin` now provides owned digital input as well as output. `c3pulse_port(0..1)`
borrows two RMT capture pairs; `c3pwm_port(0..3)` borrows four independently timed
LEDC outputs. The factories have no I/O; lifecycle setup uses the shared pin
ledger. See [timing, ownership and deferred hardware acceptance](../../../drivers/hc_sr04/INTEGRATION.md).
These two backends also use GNU99 solely for excluded SDK LL header syntax.
Compiled GPIO/PWM support is distinct from hardware qualification. The
hardware-qualified flag remains false until the relevant acceptance passes.

## SPI polling timing

The 27 October measurements exposed a SPI FIFO polling discontinuity: sleeping
after 25 ten-microsecond polls delayed each 64-byte chunk at clocks below roughly
2 MHz, producing display timeouts despite adequate wire bandwidth. The revised
adapter allows 100 bounded polls before a one-tick sleep. Interrupts remain
enabled, the caller's absolute deadline is unchanged, and long/stalled transfers
still yield. A 1 MHz chunk can now finish its nominal 512 microseconds before
that sleep threshold. Polling cannot make physically inadequate clock/deadline
pairs pass. A 400 kHz setting still exceeds the
20 ms frame deadline; corrected polling cannot compensate for inadequate
clock/deadline combinations.

Manual BARR-C review: no interface, storage, casts, language profile or ownership
changes; the unsigned counter resets at 100 and the existing finite deadline
and error propagation remain. Real target builds and loaded peer checks are
required because host simulations cannot establish FIFO/tick timing.

The I2C/SPI delay callbacks preserve the initial clock/status error before
checking the remaining deadline: a clock failure is IO, a closed session is
STATE, and an exhausted budget is TIMEOUT. Regression cases exercise all three
and a successful subsequent delay. Manual BARR-C review: this change adds no
storage, cast, API or hardware operation; checked subtraction and existing delay
bounds remain intact. Native compilation and physical timing remain separate
checks.

## Allocation observation

`c3audit` counts SDK successful heap events and calls into the kernel's separate
allocator and eleven object-creation APIs. Its fixed state and the complete heap
callback path are internal-RAM resident. Short saved/restored interrupt exclusion
protects counters on the single core; no allocation, log, scheduler lock or
pointer dereference occurs in a hook. The application marks its entry before
idle/controller initialization and seals before profile restore. Read the
[runtime contract and review](../../../src/runtime/README.md#allocation-and-object-counters)
and [wire layout](../../../src/services/TELEMETRY.md#wire-contract).
