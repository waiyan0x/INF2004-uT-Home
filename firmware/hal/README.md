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

`buslife` implements the shared lock creation, session phase and transfer-lock
transitions. `i2cown` and `spiown` retain their protocol arguments, claims/device
checks and error codes. Each owner still holds two pointers and two enums;
constant callback/error tables live in flash, with no extra heap or kernel object.
The wrappers check the lifecycle owner before reading protocol claims, then
start/stop synchronously. Workers recheck the open phase after taking the lock.

Manual BARR-C review: these C99 modules retain the synchronous callback ABI,
permanent context lifetime and serialized lifecycle owner. I2C recovers its
borrowed typed settings without a function-pointer cast. Status is initialized
before workers run; session changes occur under the permanent mutex, including
retained cleanup failures. Error domains remain protocol-specific. The shared
helpers have bounded transitions, paired headers and no hardware access, new
allocation or coding exception. The native object measurement is 16 bytes per
owner; host fault tests and a real target build check the two boundaries.

Peripheral, USB and flash backends live in `ports/esp32c3/`, including
[`c3usb`](ports/esp32c3/c3usb.c) and [`c3store`](ports/esp32c3/c3store.c).
Generic framing, transport pumping and persistence policy live in
[`src/config`](../src/config/README.md). Peripheral backends select pins from
validated profiles. Device drivers remain independent of board pin assignments
and SDK APIs.

`ports/host_sim/i2cfake.c` provides a deterministic I2C register/time fake;
`spifake.c` captures SPI transactions and injects output/transport failures.
Adding a board means adding another port without changing drivers or services.

USB provides configuration and monitoring. On-device wireless work is in the
[experimental network layer](../src/network/README.md); combined radio/service
integration and qualification remain open.

## UART

`uartio` owns one configurable byte transport with 64-byte nonblocking calls,
partial TX counts, checked cleanup and monotonic time. `uartfake` supplies host
faults; `c3uart` wraps the pinned kernel UART1. Protocol parsing remains in the
driver. See [UART integration](../drivers/hlk_ld2450/INTEGRATION.md).
Native UART hardware qualification remains pending.

`c3ukern` contains native handle creation, attributes, counts and error-code
conversion. `c3uart` owns boot-once initialization, lock/claim transitions,
bounded discard and terminal unlock failure. Host tests execute `c3uart` and
`uartio` with recorded synchronous boundary calls, including failed initialization,
retained open/close claims, RX faults and partial TX. Native ABI and wire behavior
remain target checks. The native fault-injection fixture intercepts reads in
`c3ukern`, where those calls now reside.
The RV32 objects reserve five bytes for UART lifecycle flags and two four-byte
native handles. Each GPIO adapter's shared startup state remains two bytes.

## GPIO capture and PWM

`digiin` complements `digiout` for borrowed level callbacks. `pulseio` owns a
trigger/input pair and returns bounded pulse widths; `pwmio` owns a PWM channel
with zero-duty setup/cleanup. `gpiofake` provides host faults and shared pin
claims. `c3pulse` uses RMT; `c3pwm` uses independently timed LEDC outputs.
See [limits and review](../drivers/hc_sr04/INTEGRATION.md). Physical acceptance
and generic GPIO interrupt delivery remain pending.

The shared two-Boolean `bootonce` state provisions each adapter's fixed mutex
set in order. Each partial failure stays unavailable until reboot and retains
the resources already created. Host tests inject failure at every creation
index and prove repeated startup never retries or publishes partial readiness.

Manual BARR-C review of this boundary: C99, ordinary named functions and paired
headers; unchanged synchronous native ABI and no added kernel objects or heap.
UART handles and GPIO startup state retain boot lifetime. Only the serialized
owner starts resources; workers are stopped for lifecycle changes. Native count
conversions remain bounded to 64 bytes, discard terminates at the ring/snapshot
bound, and unlock failure stops communication while retaining ownership. GPIO
creation indices are bounded by each fixed array's capacity. The host double
uses bounded logs and exact signatures through symbol redirection; it introduces
no vendor-name exemption. IRQ/register effects still require physical acceptance.
