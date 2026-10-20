# Runtime worker and activation board acceptance

Build with `tools/target/qualify_kernel.py build --runtime-port` and the selected
SDK compatibility patch; see [the tool guide](../../../tools/target/README.md).
The CMake project is intended for the helper's isolated staging directory, not
an in-place build under the repository's path with spaces.

The test uses the actual task adapter and pinned kernel. It checks:

- Dormant prepare performs no callbacks, and higher-priority workers start safely.
- Independent periodic contexts, kernel operating-time progress, rejected worker
  lifecycle re-entry, and no execution after successful cleanup/deletion.
- Sleeping workers wake on cancellation; a slow yielding step produces a stop
  timeout while retaining ownership, then a later stop completes cleanup.
- Step errors remain inspectable; failed cleanup retains the task for retry.
- Old handles cannot stop replacement tasks; a fifth worker is rejected.
- Twenty-four cycles each allocate and release all four task slots.
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
Preserve the log and repeat after reset. Kernel-create/delete error injection
and the full 16 KiB budget rejection are checked by the separate native fake-
kernel suite; do not describe those failures as physically induced on the board.

The test uses the upstream startup/32 KiB heap and memory-only fixture callbacks.
Keep peripheral wiring disconnected because the vendor startup still initializes
its native serial/ADC drivers. The fixture qualifies typed periodic-plan
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
