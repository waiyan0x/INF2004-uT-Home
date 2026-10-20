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

UART, I2C and SPI now also create their resources once at startup (see the
[C3 port guide](../firmware/hal/ports/esp32c3/README.md)). Dynamic worker tasks
and the JSON allocator are outside this change and still require the separate
system-wide migration.

## Verification record

Run `python -m unittest discover -s tests/target -p test_gpio_startup.py -v`.
Three source lifecycle guards pass: mutex creation confined to startup,
open preconditions/no handle deletion, and startup ordering before restore and
request handling. These are structural checks, not executed hardware-adapter
or kernel failure-injection tests. The board fixture also invokes both startup
functions twice to exercise idempotence when the fixture is built/run.

Pinned clang-format 20.1.8 was applied. The full style gate was run; changed
files have zero findings, but the repository remains failing with 588 findings
and six formatting errors in the six older starter files. These were not
exempted or modified. Report: `build/gpio-startup-standard.json`.

Target build was attempted but not completed: the full style gate blocks it,
the RISC-V compiler is absent from PATH, and the selected kernel/SDK sources
are not available in their documented local locations. No target compilation,
new map sizes, physical tests or whole-system compliance is claimed. Existing
qualification records remain untouched. Before merging, resolve the existing
gate failures, restore the pinned toolchain/dependencies, build both application
and fixture modes with `tools/target/qualify_kernel.py`, and verify repeated
open/close/configuration replacement plus mutex-exhaustion startup on target.

Manual BARR-C review: C99-owned syntax and existing SDK dialect boundary kept;
named paired-header APIs; bounded loops (2/4); positive IDs only retained;
static flags initialized; no allocator, new casts or ISR changes; setup calls
checked before normal operation; partial failures explicitly retained; no
exception or policy changes. Runtime owner serializes lifecycle calls and
affected workers must be stopped before close. Electrical behaviour still
requires bench acceptance.
