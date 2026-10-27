# Runtime worker and activation board acceptance

Build with `tools/target/qualify_kernel.py build --runtime-port` and the selected
SDK compatibility patch; see [the tool guide](../../../tools/target/README.md).
The CMake project is intended for the helper's isolated staging directory, not
an in-place build under the repository's path with spaces.

The ordinary runtime fixture leaves the attached UART peripheral unused. Native
ring injection/transmission is a **separate** `--native-uart-fixture` build that
requires external UART senders and receivers disconnected. It retains every
fault check; its PASS marker is `[uart-test] PASS isolated native ring faults`.
The ordinary fixture prints `[uart-test] NOT RUN` and must not be reported as
native UART fault acceptance. The initial 27 October fixture selected the older
combined mode and failed the no-external-sender check with the radar attached;
that failed capture is retained. Earlier sections below describe historical
images and their original evidence, not the latest source's acceptance status.

The test uses the actual task adapter and pinned kernel. It checks:

- Dormant prepare performs no callbacks, and higher-priority workers start safely.
- A higher-priority contender preempts a task holding the shared-data guard,
  yields without owning it, and completes its operation in the same call after
  release. Same-task recursion remains rejected. The handoff PASS marker is
  repeated in the final summary because USB re-enumeration can hide early output.
- Independent periodic contexts, kernel operating-time progress, rejected worker
  lifecycle re-entry, and no execution after successful cleanup/binding release.
- Sleeping workers wake on cancellation; a slow yielding step produces a stop
  timeout while retaining ownership, then a later stop completes cleanup.
- Step errors remain inspectable; failed cleanup retains the task for retry.
- Old handles cannot stop replacement tasks; a fifth worker is rejected.
- Twenty-four cycles each bind and release all four permanent task slots.
- A closed dispatch gate prevents steps while the owner starts other workers.
- Two configured fixture components exchange copied distance records through
  the serialized queue API; replacement changes values without runtime edits.
- Enable/disable stops the consumer; a worker step fault is visible in status.
- Failed configuration restores old settings. Failed cleanup or restoration
  retains ownership until an explicit recovery succeeds.
- Six plan replacements reuse resources; empty-plan shutdown removes workers
  and stale watchdog registrations.

Success prints `[activation-test] PASS`, `[port-test] PASS` and continuing
`alive` counters. The 2026-09-28 activation image passed 514 checks per reset.
A failed check
prints its expression and line, then keeps the board alive without proceeding.
Preserve the log and repeat after reset. Startup-create and binding/state-query failure injection
and the full 16 KiB budget rejection are checked by the separate native fake-
kernel suite; do not describe those failures as physically induced on the board.

The test uses the upstream startup/32 KiB heap and memory-only fixture callbacks.
Use the [installation procedure](../../../tools/target/README.md#install-the-configuration-application)
for the current resource-patched build. Its reviewed BMP388/OLED/LD2450 layout
may remain assembled with no actuator attached; other wiring needs review.
The resource patch disables unused ADC startup. The fixture qualifies typed periodic-plan
activation and queue/health synchronization. No external device rollback,
profile parser, USB configuration, stream readiness, power-cycle recovery or
absolute clock accuracy is established by this test.

The build helper snapshots the exact adapter, activation/shared modules and
their component/queue/health/watchdog dependencies. Preserve historical records;
new runs belong in separate build and board evidence files.

The I2C software checkpoint also links `i2cbus`, `c3i2c`, the BMP388 adapter
and the unchanged Bosch library. Five invalid-argument/descriptor checks retain
that code in the link without touching pins. This produces an expected 519-check
acceptance image, but that revised image has not been flashed or board-qualified.
The historical 514-check logs apply only to their recorded source hashes.
Actual sensor communication needs a separate configured hardware test after the
breakout wiring is confirmed; this default fixture never opens a real bus.
The fixture now also checks that `c3i2c_init()` returns 0 on its first call
and again on a repeated call. These two checks test return values only, and
the image is unqualified. The proof that only one mutex is created comes from
the host `owntest` creation-call counter, not from this fixture. The same two
return-value checks for `c3spi_init()` add two more; for SPI that proof comes
from the host `spowntest` counter.

The subsequent SPI/OLED checkpoint adds SPI2, shared pin ownership, simple outputs
and the OLED driver/registry adapter. Eight further argument/descriptor checks
give an expected **527 checks** in this image. It is build-only: no new board
run is claimed. No bus or output is opened. All peripheral hardware testing is
deferred to final acceptance, and the historical 514-check image remains the
last physically qualified version.

The UART checkpoint adds three factory/descriptor/invalid-argument checks,
for an expected **530 checks**. UART is never opened by the default fixture.
The new build applies the existing resource patch in its isolated source;
that startup change and the UART wrapper still need physical acceptance.
The latest actually captured image remains the earlier 514-check activation
image. Host radar/ownership scenarios live in `tests/target/uarttest.c`.

## GPIO software checkpoint

Thirteen additional no-I/O factory/descriptor/invalid-argument checks bring the
fixture to **543 expected checks**. This revised image has not been flashed or
executed. Native RMT/LEDC and GPIO electrical tests remain deferred; host
contracts and recovery are exercised by `tests/target/gpiotest.c`. See
[the manual review](../../../firmware/drivers/hc_sr04/INTEGRATION.md).

## Configuration core software checkpoint

Nine additional invalid-argument checks link the portable configuration core,
bringing the fixture to **552 expected checks**. No physical USB/storage
operation is invoked. This image has not been flashed or executed; host codec,
controller and interrupted-save tests are in `cfg_main.c` and
`test_config_transfer.py`. See
[the review](../../../firmware/src/config/README.md#wire-version-1).

## Admission/binding software link check

New C99 modules and managed cJSON 1.7.19~2 are linked, with nesting limited to
eight. Ten no-I/O additions give **562 expected checks**, not a board capture.
The large binder is not allocated by these link/invalid-input checks. See
[admission contracts](../../../firmware/src/config/README.md#admission).
The historical 514-check activation image remains physically qualified.


## Current core and radio diagnostic scope

The current isolated core fixture additionally checks sampled native scheduler
time: a 100 ms delay advances elapsed/idle counters with a one-ms tick. The
scheduler-enabled fixture passed 1,699 checks on each of two resets. Its native
stack checks run after quiescence; unavailable live worker margins remain null.
The root README identifies the current installed image and active capture owner.

BLE diagnostics are separate from these core checks. `--ble-probe` selects
nonconnectable advertising; adding `--ble-gatt-probe` selects a 45-second read-only
uptime service, with one connection permitted and no configuration commands.
See the [network guide](../../../firmware/src/network/README.md#read-only-gatt-diagnostic)
for its contract and C review. SDK allocations remain exposed and unqualified.

## Experimental model resource probe

`--runtime-port --static-net-queues --model-probe` isolates the frozen primary
study classifier. It retains the inactive USB core and four permanent workers,
but runs no peripheral traffic, radio, advice service or Apply. The model failed
its statistical benefit gate; this diagnostic never changes that decision.
The qualifier rejects model symbols in ordinary/application/radio images and
rejects radio activation in the model probe. Both source snapshots and the six
fixture source hashes are verified before accepting a build.

`model/fixture.json` binds the experimental model identity and 1,165 immutable
Python prediction vectors (measured workload vectors, domain/split boundaries,
out-of-domain inputs and seeded random values). Expected predictions are not
accuracy labels. The exported module is renamed from `protree` to `aifrozen`
solely to preserve one paired header per module; its classifier is unchanged.
The test corpus stays in flash and is separate from inference memory claims.

The probe compares every prediction, then times 256 four-candidate batches.
Timer/call overhead and possible preemption remain included, with a separate
empty clock-pair observation and no subtraction. Audit counters must remain
unchanged; batch wall time must stay within 10 ms. Invalid input also rejects.
This measures an unloaded diagnostic, not loaded request latency or sampling
interference in the finished application. C compiler stack reports and linked
model sections are retained separately from test-corpus/harness storage.

Manual BARR-C review (rules 1?8): paired modules, C99, immutable input corpus,
bounded 1,165-vector/256-batch loops, fixed-width feature words, checked clock
ordering/narrowing and checked cumulative time. Four-candidate indices remain
within the corpus; checksum additions cannot exceed 2,048. Native counter fields
are compared individually, never by structure padding. No heap, kernel-object
creation, recursive calls, configuration mutation or new deviation is added.
