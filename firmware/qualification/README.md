# ESP32-C3 kernel qualification

On 2026-09-28 the pinned kernel task example built with ESP-IDF v6.1 and the
tracked CMake compatibility patch, then **passed startup and repeated USB-reset
checks** on the ESP32-C3 SuperMini connected as COM8. Chip identification confirmed
an ESP32-C3 revision 0.4 and 4 MB flash. Its original flash was backed up before
loading the example; esptool verified all three written image segments.

The [build record](esp32c3-kernel-20260928.json) retains the original build-only
result and artifact hashes. The separate [board record](esp32c3-board-20260928.json)
adds device identification, backup and capture hashes, reset behaviour and limits.
Later sections record project runtime qualification separately. Peripheral
drivers remain unqualified.

| Item | Evidence |
|---|---|
| Kernel source | `ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` |
| SDK | ESP-IDF v6.1, `fff9895c82d744c7237be8847347bdd1b07c6643` |
| Compiler | GCC 15.2.0, `esp-15.2.0_20251204` |
| Other tools | CMake 4.0.3, Ninja 1.12.1, Python 3.14.2 |
| Example / board kit | `examples/tasks` / `iote_esp32c3_mini` |
| Compatibility patch | `0002-esp-idf-6.1-build.patch` |
| Main image | 151,008 bytes; fits the 1 MiB application partition |
| Image SHA-256 | `ee6703bde0e1531f4956c88c1d77761e1424d9d434752e7a9106a93aa4685298` |
| Project submodule | Still pristine at the pinned revision |
| Target runtime adapter | Tested separately below; this upstream example does not use it |

Static ELF inspection found `usermain`, `knl_main`, `tk_cre_tsk`, `tk_sta_tsk`
and the startup wrapper. Board captures now show the startup handover into
`knl_main()`, the microT-Kernel 3.00 banner and the task example running.

| Complete capture | Reset | Observed task output |
|---|---|---|
| [boot 3](logs/esp32c3-20260928-boot-3.txt) | USB chip reset | A and B alternate, each counting 0 through 24 |
| [boot 4](logs/esp32c3-20260928-boot-4.txt) | USB chip reset | A and B restart from 0 and alternate through 24 |

Each capture was configured for 25 seconds and contained one startup, with no
observed panic or unexpected restart. Repository log copies normalize line endings
and trailing whitespace; raw captures and both sets of hashes are retained.

The first USB reset stayed in ROM download mode. esptool's watchdog reset then
started the tasks, but that capture missed early startup. Subsequent USB resets
produced the two complete logs above. This recovery did not modify the firmware.

The ESP-IDF application initialization timestamps are unusually large and repeat
across startup lines. They are not accepted as a runtime clock or timing evidence.
Printed task progress does not establish latency, jitter or scheduling bounds.

## Reproduction and retained diagnostics

The isolated local installations are under `%USERPROFILE%\esp`:

- `uthome-c3-sdk-v6.1`: pinned SDK with its submodules.
- `uthome-c3-tools`: compiler/build tools and isolated Python environment.
- `uthome-board-qualification-20260928`: original 4 MB flash backup, backup/flash
  diagnostics and raw boot captures from the upstream task-example run.
- `uthome-kernel-qualification-20260928-compat1`: successful build, staged source,
  patch copy, `build.log`, `sdkconfig`, `evidence.json` and firmware artifacts.

The first attempt (`uthome-kernel-qualification-20260928`) exposed backslashes
in the SDK environment path being interpreted as CMake escapes. The helper now
normalizes the child process path and enables UTF-8 without system-wide changes.
The second, unmodified-source attempt (`...-20260928-run2`) then failed because
`hal/systimer_hal.h` was absent from the shadow FreeRTOS component's include path.
The tracked compatibility patch declares the SDK's `esp_hal_systimer` dependency
in both affected components. The task example and kernel C files are unchanged.

Follow [the target tooling guide](../../tools/target/README.md) to reproduce the
patched build in a new directory, or omit the patch to reproduce the upstream
failure. Full build logs and binaries stay outside Git; hashes identify this run.

## Next qualification boundary

The subsequent [runtime task-port record](esp32c3-runtime-20260928.json) covers
the implemented periodic-worker lifecycle: two reset runs each passed 369 checks,
including cooperative stop, retained ownership on timeout and cleanup retry.
Its [build record](esp32c3-runtime-20260928-build.json) identifies the exact project
sources and artifacts. Logs: [first run](logs/esp32c3-runtime-20260928-boot-1.txt),
[repeat run](logs/esp32c3-runtime-20260928-boot-2.txt).

The kernel operating-time clock advanced 101 ms during a requested 100 ms delay;
this is a basic progress check, not an independent clock-accuracy measurement.
The earlier ESP-IDF logging timestamps remain unsuitable for this purpose.

Next, integrate checked whole-plan activation and restoration, serialize owned
queue/health resources, then connect a real peripheral through the generic
registry. Target capabilities stay empty and AI training stays paused until that
application path is demonstrated. The board currently runs the task-port test;
its original firmware backup remains retained in the earlier board directory.

A physical power-cycle test and the upstream root demo's broader kernel API smoke
tests were not performed. The two-task startup/reset result does not claim those
checks passed. See [the integration record](../src/runtime/INTEGRATION.md).

## BARR-C refactor regression evidence (2026-09-28)

The [BARR-C build record](esp32c3-barr-20260928-build.json) identifies the renamed
and refactored sources, pinned toolchain, standard-gate result and artifacts.
The [board record](esp32c3-barr-20260928.json) documents two reset captures, each
passing the original 369 checks and 24 four-slot allocation/release cycles.
Logs: [first reset](logs/esp32c3-barr-20260928-boot-1.txt) and
[second reset](logs/esp32c3-barr-20260928-boot-2.txt).

These records superseded the earlier runtime record for that acceptance image;
historical records remain unchanged. The standard review and
compatibility sign-off are documented in
[the audit](../../tools/c_standard/AUDIT.md).

## Periodic plan activation (2026-09-28)

The [activation build record](esp32c3-activation-20260928-build.json) identifies
the 169,472-byte app and exact 23-file source snapshot. The
[board record](esp32c3-activation-20260928.json) documents two USB-reset captures,
each passing **514 checks**, including the earlier worker checks and the new
plan activation/recovery fixture. Logs:
[first reset](logs/esp32c3-activation-20260928-boot-1.txt) and
[second reset](logs/esp32c3-activation-20260928-boot-2.txt).

Both runs verified a start barrier, typed producer/consumer queue, enable/disable,
replacement, asynchronous worker error reporting, failed activation/restoration,
retained cleanup ownership, explicit recovery, six replacement cycles and
empty-plan shutdown without stale heartbeat registrations. Each 22-second
capture contains one startup, 514 successful checks and continuing alive counts;
no failure, panic or unexpected restart was observed after reset.

Only the app at `0x10000` was flashed and verified. The prior bootloader and
partition table were preserved; a full 4 MiB backup of the preceding firmware
is retained under `%TEMP%/uthome-activation-u7552i33/`. Its hash and the earlier
original-firmware backup are recorded in the board evidence. The first raw
capture includes a short pre-reset fragment, excluded from its normalized log.
Builds, raw captures and flash diagnostics remain under
`%USERPROFILE%/esp/uthome-activation-qualification-20260928/`.

The board now runs this acceptance image. It exercises periodic test components,
not external devices or a USB profile loader. The next checkpoint is I2C/BMP388
integration. AI, stream readiness, physical power-cycle recovery, flash
persistence and workload timing measurements remain deferred.

The mandatory standard gate passed for 70 C/header files; all 113 host tests
passed. New activation/shared modules separately compile as C99 with warnings
as errors. [The activation guide](../src/runtime/ACTIVATION.md) records manual
review and ownership rules. No compatibility exception was added or broadened.

## I2C and BMP388 software checkpoint (2026-09-28)

The [software record](esp32c3-i2c-20260928.json) records **114 passing host tests**
and the mandatory standard gate for **82 owned C/header files**. The
[build record](esp32c3-i2c-20260928-build.json) identifies the exact 36-input
snapshot, effective compiler profiles and 180,304-byte application image
(`47123da4abb4930b4336edbac30a0a9a1299bcb6140066f2c84430d698dbebb2`).
The image fits the existing 1 MiB application partition.

The reusable bus contract, C3 controller backend, unchanged Bosch library and
BMP388 instance adapter compile together. Host fixtures exercise two independent
sensor contexts, typed samples, deadlines, duplicate-address rejection, device
errors and activation cleanup/recovery. The
[integration guide](../drivers/bmp388/INTEGRATION.md) records ownership rules,
manual BARR-C review and the boundary between synthetic tests and hardware proof.

Qualification now checks the actual compiler commands: ordinary owned modules
use C99; only the two previously approved atomic files use C11. The C3 I2C adapter
uses GNU99 because included, excluded-from-audit SDK headers require `typeof`;
its owned source remains C99. Enabled warnings are errors. The deviation and
approval manifests are unchanged. Earlier local build attempts exposed SDK flag
overrides and the vendor-header requirement; their diagnostics are retained and
historical qualification records have not been rewritten.

**This image has not been flashed or run on the board.** Its five added peripheral
checks only validate arguments without opening the bus. The previous activation
image and its 514-check captures remain the latest physical qualification.
The BMP388 is available but unwired; no physical I2C, electrical timing, sensor
accuracy or shared-bus contention result is claimed. The target-pending
peripheral manifest remains empty, and AI work remains deferred.

The final build, logs and compiler database are retained under
`%USERPROFILE%/esp/uthome-i2c-qualification-20260928-qualified/`; host logs and the
pre-checkpoint source backup are under `%TEMP%/uthome-i2c-qxvy2g1q/`.
The next acceptance step is to confirm the breakout, supply/pull-ups, I2C mode,
address and wiring, then exercise a real sensor and cleanup/recovery on the board.

## SPI/OLED software checkpoint (2026-09-28)

The [software record](esp32c3-spi-20260928.json) records 114 passing host tests and
the mandatory gate for 99 owned C/header files. The
[build record](esp32c3-spi-20260928-build.json) identifies 49 inputs, effective
compiler profiles and the 188,208-byte application (within the 1 MiB partition).
SHA-256: `8338026604576abcdf326a2be36b342824f9b09d89bdeb9aacf45ec5e49d0410`.

New modules cover shared SPI, pin ownership, basic outputs and independent OLED
frames. I2C now uses the same pin ledger. The real C3 adapters compile as GNU99
only because excluded SDK headers require it; owned source remains C99. The two
approved C11 paths and compatibility manifests remain unchanged.

This image is build-only and has not been flashed. Its eight added argument
checks would bring the default fixture to 527 checks without opening peripherals.
The earlier activation image with 514-check captures remains the latest physical
qualification. Hardware tests now wait until final acceptance at the user's
request. SPI completion cannot establish SSD1306 presence or visible correctness.
See [manual review and limits](../drivers/ssd1306_spi/INTEGRATION.md).

The build, compiler database, logs and artifacts remain under
`%USERPROFILE%/esp/uthome-spi-qualification-20260928/`; the source backup, checked
copy and host logs are under `%TEMP%/uthome-spi-u9pdvfu7/`.

## UART/LD2450 software checkpoint (2026-09-28)

The [software record](esp32c3-uart-20260928.json) identifies 115 passing host tests and a
clean mandatory gate for 113 owned C/H files. The [build record](esp32c3-uart-20260928-build.json)
records 59 inputs, 24 owned compiler profiles, both unchanged kernel patches,
and the 192,336-byte application.
SHA-256: `bfdf730a374e6f6d183f5452238bea2328aa7f6fd0fa23c30a4eb5caf7c943f0`.

Portable UART ownership, the native UART1 wrapper, bounded radar parsing and
registry/typed-queue integration are included. See
[manual review and limits](../drivers/hlk_ld2450/INTEGRATION.md).
The final reviewed build is under `%USERPROFILE%/esp/uthome-uart-qualification-20260928-final/`;
backup, host logs and checked sources are under `%TEMP%/uthome-uart-yjdkni5h/`.

No flash/reset/UART activity was performed. Three added factory/argument checks
give an expected 530-check board fixture, not a captured result. The previous
514-check activation image remains the latest physical qualification. The new
resource-patched startup and UART timing await final hardware acceptance.

## GPIO distance/PWM software checkpoint (2026-09-28)

The [software record](esp32c3-gpio-20260928.json) identifies 115 passing host tests and
the mandatory gate for 132 owned C/H files. The [build record](esp32c3-gpio-20260928-build.json)
records 74 inputs, 31 owned compiler profiles, both unchanged kernel patches,
and the 199,408-byte application.
SHA-256: `3f9ce4f3c8004d8b492f16cf33fa1034806ac7978016893ceeb7d229066ae207`.

Reusable GPIO/pulse/PWM interfaces, C3 RMT/LEDC, HC-SR04 typed publication and
generic PWM component writes are included. See
[manual review and remaining limits](../drivers/hc_sr04/INTEGRATION.md).
The build is under `%USERPROFILE%/esp/uthome-gpio-qualification-20260928-final/`;
backup, checked sources and host logs are under `%TEMP%/uthome-gpio-45hzvly4/`.

No flash/reset or physical I/O was performed. Thirteen additional no-I/O board
checks give **543 expected checks**, not a captured result. The earlier
514-check activation image remains the latest physical qualification.
RMT edge handling, PWM behavior and combined peripherals await final acceptance.

## Configuration core software checkpoint (2026-09-28)

The [software record](esp32c3-config-core-20260928.json) identifies 119 passing host tests and
the mandatory gate for 142 C/H files. The [build record](esp32c3-config-core-20260928-build.json)
records 82 inputs and 35 owned compiler profiles, with unchanged kernel/SDK
pins, resource patches and approved compatibility scope.
Application: 204,320 bytes;
SHA-256 `03fb9130bfc7df0071d4f4208705ac9029b658a22cb292bc295ab9c036938c68`.

The portable framing, controller, two-slot storage and Python codec are verified
in software. See [manual review and production gaps](../src/config/TRANSFER.md).
The build is `%USERPROFILE%/esp/uthome-config-core-qualification-20260928/`;
backups and host logs are `%TEMP%/uthome-config-a8r82zwt/`.
Nine no-I/O fixture additions give 552 expected checks, not a captured result.
There was no flash/reset/serial/storage operation on the board. The prior
514-check activation image remains the latest physical qualification.

## Admission/binding software checkpoint (2026-09-28)

The [software record](esp32c3-admission-20260928.json) identifies **125 passing host tests**
and the mandatory gate for **158 owned C/H files**. The
[build record](esp32c3-admission-20260928-build.json) retains 100 inputs and 41 owned compiler
profiles. The [resolved managed lock](esp32c3-admission-20260928-dependencies.lock) pins cJSON;
its compiled source bytes match the retained host mirror.

Application: 223,152 bytes;
SHA-256 e834faa40f4a77f161f1af99126f0262522698bed9fce944e07266e923b8702f.
The build is under %USERPROFILE%/esp/uthome-admission-qualification-20260928/.
Backups and raw host logs are %TEMP%/uthome-admission-erx9zd8u/.
See [manual review and production gaps](../src/config/ADMISSION.md).
562 fixture checks are expected, not a board capture. No physical operation
was performed; the historical 514-check activation image is unchanged.

## USB/storage application software checkpoint (2026-09-28)

The [software record](esp32c3-usbstore-20260928.json) records **134 passing host tests** and the
mandatory BARR-C gate across **166 C/H files**. Both the
[configuration application build](esp32c3-usbstore-20260928-application-build.json) and
[acceptance fixture build](esp32c3-usbstore-20260928-fixture-build.json) passed, each with 110
source inputs and 45 owned compiler profiles. The
[managed dependency lock](esp32c3-usbstore-20260928-dependencies.lock) retains the pinned cJSON.

Application image: 192,320 bytes;
fixture image: 227,344 bytes.
Artifact SHA-256 hashes and exact build paths are in the records. Application
contexts occupy 39,024 static bytes, before other runtime state,
the parser's bounded heap requests and task stacks. This is not a free-RAM or
stack-high-water measurement. See [manual review and limits](../src/config/USB_STORAGE.md).

Builds: `%USERPROFILE%/esp/uthome-usbstore-app-20260928b/` and
`%USERPROFILE%/esp/uthome-usbstore-fixture-20260928/`.
Snapshot/logs: `%TEMP%/uthome-usbstore-q6fcfgck/`.
568 fixture checks are expected, not a captured hardware result. No board was
flashed/reset or accessed over serial; the prior 514-check activation evidence
remains the latest physical qualification.

## Telemetry/health software checkpoint (2026-09-28)

The [software record](esp32c3-telemetry-20260928.json) records **140 passing host tests** and the
mandatory BARR-C gate across **174 C/H files**. Both the
[application build](esp32c3-telemetry-20260928-application-build.json) and
[acceptance fixture build](esp32c3-telemetry-20260928-fixture-build.json) pass with **116 inputs**
and **48 owned compiler profiles** each. The
[dependency lock](esp32c3-telemetry-20260928-dependencies.lock) preserves pinned managed cJSON.

Application image: 199,120 bytes;
fixture image: 229,632 bytes.
Exact artifact hashes and build paths are recorded. Selected application contexts
plus the live sample cache occupy 44,256 static bytes; this
does not establish free heap or stack high-water marks. See
[manual review and memory details](../src/services/TELEMETRY.md).

Builds: `%USERPROFILE%/esp/uthome-telemetry-app-20260928/` and
`%USERPROFILE%/esp/uthome-telemetry-fixture-20260928/`.
Snapshot/logs: `%TEMP%/uthome-telemetry-5j_hd4pr/`.
The existing fixture still expects 568 checks, not a captured hardware result.
No board access/flash/reset occurred; prior physical evidence is unchanged.

## Configured OLED software checkpoint (2026-09-28)

[Checkpoint 6b evidence](esp32c3-display-20260928.json) records 140 passing host tests, the
176-file BARR-C gate and both real target builds (118 inputs, 49 owned compiler
profiles each). Application/fixture binaries are respectively
209,072/
233,568 bytes.
Build paths, artifact/source hashes and the dependency lock are recorded there.
See [manual review and memory limits](../src/services/DISPLAY.md).
The 568-check fixture was built, not run on hardware; prior physical evidence,
dependencies, partitions and policy approvals remain unchanged.

## Local application and rules software checkpoint (2026-09-28/29)

[Checkpoint 6c/7 evidence](esp32c3-ui-rules-20260928.json) records 151 passing host tests, the
180-file BARR-C gate, browser checks and both real target builds (122 inputs,
51 owned compiler profiles each). Application/fixture binaries are respectively
214,992/
237,200 bytes.
Source/artifact hashes, dependency lock and memory accounting are recorded there.
The 568-check board fixture was built, not run. Prior physical evidence and
partition table are unchanged. Hardware acceptance and AI remain deferred.
