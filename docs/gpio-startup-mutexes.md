# GPIO mutex lifetime change

The C3 pulse and PWM adapters now expose `c3pulse_startup()` and
`c3pwm_startup()`. The lifecycle owner calls them after `activate_init()` and
before restoring any configuration or processing USB requests. `c3app_run`
performs this sequence and aborts startup on error. Alternate applications must
perform the same startup calls; borrowing a port does not allocate anything.

Two pulse mutexes and four PWM mutexes are created once per boot. Repeated
startup calls after success return success without allocation. A failed attempt
retains any already-created mutexes, returns an error on subsequent calls, and
requires reboot. This prevents an attempted recovery from creating objects
during normal operation. The startup functions must never be first invoked
after normal operation begins; this boundary is enforced by the application
call order, not a new global kernel phase detector.

Open rejects uninitialized adapters, existing hardware users and partial pin
claims. Close still stops output/capture, releases pins and reference-counted
peripheral clocks, but never deletes the mutex. A normal close/open cycle
therefore uses the original handle. Failed cleanup retains claims for retry.
The stopped-worker/serialized lifecycle requirement remains unchanged. No
changes were made to timing, duty conversion, GPIO assignments, public portable
HAL operations, sensor drivers, configuration schemas or the relay additions.

## RAM and remaining static-allocation work

Reserve six kernel mutex control blocks at startup regardless of the enabled
profile. Their IDs already occupy the adapter state structures. Two Boolean
startup flags are added per adapter; linker padding and actual kernel control
block sizes must be obtained from the target build/map, not inferred from the
host. Check the total kernel mutex capacity together with UART/I2C/SPI and other
services. Startup fails closed if capacity is exhausted. This does not prove
that the kernel's own creation implementation is heap-free.

UART, I2C and SPI also create their resources at startup. Fixed workers and
parsing are implemented; see the [C3 port](../firmware/hal/ports/esp32c3/README.md)
and [system status](../README.md#current-status). Remaining system work concerns
wireless resources and combined acceptance, not a repeat of the worker migration.

## Verification

Run `python -m unittest discover -s tests/target -p test_gpio_startup.py -v`.
Structural source checks do not execute kernel/hardware failure paths. The
board fixture invokes startup twice for idempotence; verify creation counters
and mutex-exhaustion behavior separately. Physical pulse/PWM timing and output
loads still require acceptance. Historical failed-gate records are retained in
the [history log](COMPLETED_WORK_LOG.md); they are not current build blockers.

Manual BARR-C review: C99-owned syntax and existing SDK dialect boundary kept;
named paired-header APIs; bounded loops (2/4); positive IDs only retained;
static flags initialized; no allocator, new casts or ISR changes; setup calls
checked before normal operation; partial failures explicitly retained; no
exception or policy changes. Runtime owner serializes lifecycle calls and
affected workers must be stopped before close. Electrical behaviour still
requires bench acceptance.
