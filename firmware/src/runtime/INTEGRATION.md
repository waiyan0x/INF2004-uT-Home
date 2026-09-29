# Member 1 runtime integration record

Initial review on 2026-09-27 against platform baseline 2.0.0.
Checkpoint 2 completed on 2026-09-28; the progress section at the end records
the current target activation path. Earlier sections retain their review scope.

The subsequent BARR-C:2018 pass on 2026-09-28 shortened C API identifiers,
paired source/header names, clarified declarations and added mandatory build
checks. See [the C standard and migration notes](../../../C_CODING_STANDARD.md).
The lifecycle and ownership contracts below remain unchanged. Historical board
records retain their original source hashes; new source needs fresh qualification.

## Source and scope

Local tracking revision: `origin/feat/runtime-utkernel-adapter` at
`2cd87412cf4684e7d7e0be9f0e50e19b7ec48eb3`. This stacked branch includes the
lifecycle, resources, metrics, runtime ports, message bus, health, watchdog and
host integration/fault tests. GitHub fetch required credentials and could not
verify newer remote commits. No branches were merged or pushed in this pass.

The existing component/registry and configuration planner are retained. The
runtime header preserves the device layout and baseline APIs while incorporating
Member 1's status/resource/lifecycle APIs and adding per-instance bindings.
Queue/health modules and their tests are reused. Host port/runtime implementations
are reconciled around checked task ownership and host transactions.

## Changes justified by review

| Finding in the branch | Integrated behaviour |
|---|---|
| Task release ignored backend stop failure | Keep ownership until stop succeeds; transaction restores prior state on failure |
| Reconfiguration could mutate metadata before backend acceptance | Restore scheduling, lifecycle state and host resources together |
| Stack-size changes were metadata updates | Recreate the host task resource within the transaction |
| Runtime counters did not execute registered components | Optional bindings dispatch bounded steps using independent instance contexts |
| No whole-plan resource rollback | Checkpoint host task resources and runtime state; quarantine if restoration fails |
| Target port was an unimplemented stub | Implement explicit periodic workers; keep native selection guard and unsupported transaction failures |
| Watchdog silently truncated overlong IDs | Reject IDs that cannot be represented without truncation |

These transactions cover host task metadata only. An in-memory checkpoint is
not a valid implementation of kernel task or physical device rollback.

## Acceptance evidence

Run `python -B -m unittest discover -s tests/foundation -v` and
`python -B -m unittest discover -s tests/runtime -v` from the repository root.
GCC compiles the C suites with `-Wall -Wextra -Werror -pedantic`.

The foundation fixture projects driver identity, instance identity and scheduling
from validated plans into the actual registry and bound runtime. Two sensor
instances and a distance-only plan use the same binary; changing timing and
disabling an instance changes observed callback counts. Registry entries in
that fixture are counters, not real peripheral drivers. It does not deserialize
an entire plan in firmware or validate target wiring.

Additional C suites cover independent contexts and typed queue publication,
stream readiness dispatch, no I/O during apply, callback re-entry rejection,
configuration/stack rollback, partial replacement failures, retained bindings,
resource leaks, failed recovery quarantine and the target adapter's failure guard.
Imported Member 1 suites continue checking lifecycle, metrics, queues and health.

## Next implementation boundary

Member 1 can use the pinned kernel source already documented in
[SETUP_ESP32C3.md](../../SETUP_ESP32C3.md); no repository choice is outstanding.
The next target work is:

1. Task-example board gate passed on 2026-09-28: COM8 is an ESP32-C3 revision
   0.4 with 4 MB flash. Two complete captures show the kernel startup and A/B
   counters 0 through 24 after USB resets. See
   [the qualification record](../../qualification/README.md). This does not
   qualify application workers, kernel stop/delete behaviour or timing accuracy.
2. Periodic worker primitives are implemented in `tkport.c`,
   selected explicitly with `UTHOME_RUNTIME_UTKERNEL`. Review the borrowed-context,
   single lifecycle-owner and retryable-cleanup contract in
   [workport.h](workport.h). Streaming readiness and
   higher-level runtime binding remain outstanding. Board evidence is recorded
   separately from the earlier upstream task-example test.
3. Implement checked activation and restoration for real kernel/HAL resources;
   preserve ownership if cleanup fails. Do not copy the host snapshot strategy
   onto live kernel objects. Define recovery when restoration cannot complete.
4. Serialize runtime, queue and health access across actual tasks/interrupts.
   Add watchdog removal/replacement and per-instance queue/health ownership.
   Do not automatically heartbeat on a timer without evidence of completed work.
5. Integrate one real peripheral adapter through the registry, then verify a
   second independent instance and live reconfiguration using the same core.
   Map complete normalized settings, routing and descriptor checks on target.

Members 2 and 3 need to supply per-instance HAL/driver contexts, bounded timeouts,
cleanup hooks, supported operations and sample layouts for their adapters.
Member 5 needs to use planner output, map queue names/types, and implement
transfer plus persistence only after checked activation. These are integration
deliverables, not blockers to this host reconciliation. Target capabilities stay
empty and AI training stays paused until the platform path is demonstrated.

## 2026-09-28 qualification progress

Installed an isolated pinned SDK/toolchain, added a reproducible build/evidence
helper and fixed the observed missing `esp_hal_systimer` CMake dependencies through
a tracked patch applied only to staged sources. The kernel task example now
compiles; the kernel submodule remains pristine. The board subsequently passed
the task-example startup and repeated USB-reset checks. Its original 4 MB flash
was backed up before flashing. The subsequent target worker implementation
provides prepare/start/stop, cleanup retry, dormant reconfiguration and operating
time. Its whole-plan transaction APIs remain unsupported. The generic runtime
still requires activation/recovery and shared-resource integration.

The captured ESP-IDF application initialization timestamps are unusually large
and unchanged across those startup lines. Do not use them as timing evidence.
The new port uses `tk_get_otm` instead, and the board test verifies operating-time
progress across a kernel delay. Independent clock accuracy and workload/jitter
measurements remain outstanding; the original task output only establishes
observed progress.

The application startup guide now explicitly delays last-known-good persistence
until activation succeeds, preserving the preceding configuration for recovery.

## Runtime task-port acceptance

The project adapter built against the pinned kernel/SDK and passed **369 checks
on each of two USB-reset runs** on COM8. Two contexts executed independently
(fast: six calls; slow: three), sleeping-worker cancellation completed within
the clock's millisecond resolution, an 800 ms yielding step caused a bounded
stop timeout with retained ownership, and a later stop cleaned/deleted it.
Step failure, failed-cleanup retry, stale-handle rejection and 24 four-slot
allocation/release cycles also passed. No unexpected reset or panic was observed
in either 20-second capture. See [the evidence](../../qualification/esp32c3-runtime-20260928.json).

Native validation passed 23 foundation tests, 17 runtime C suites and 11 target
tests. The fake-kernel suite additionally covers injected kernel creation,
start, priority-update and deletion failures, plus stack-budget rejection.
Those injected kernel failures are not claimed as physical board tests.

The application-level transaction/activation gate remains closed. The next
implementation boundary is binding admitted instance plans to these workers
with checked activation/recovery and queue/health ownership. No additional
member input was required for this task-port step. Before peripheral acceptance,
confirm the actual module, wiring/pins, supply voltage and bus address; Member 2
or 3's driver must expose finite operations and retryable cleanup for each
independent context. That checkpoint left the board running the task-port image.

## Checkpoint 2: periodic plan activation

The separate target `activate` module now binds resolved component contexts
to real workers, with whole-plan replacement and explicit recovery. It copies
bounded configuration values, validates task/queue limits and declared consumer
routes, stops all old workers, checks cleanup/deletion and starts replacements
behind a closed dispatch gate. Failed activation reconstructs prior settings;
failed cleanup/restoration retains ownership and blocks further apply calls.
The original host snapshot APIs remain unsupported on target.

`shared` wrappers serialize short queue, health and watchdog calls with a
nonblocking task-context guard. Whole-plan reset removes old registrations
after workers are released. Completed steps update heartbeats; later worker
failures remain visible in activation status. See
[the API, ownership and manual review](ACTIVATION.md).

Validation: 70 C/header files passed the mandatory standard gate, 113 host
tests passed, and the two new modules separately compiled as C99 with warnings
as errors. The pinned target build passed. Two complete USB-reset captures on
COM8 each passed **514 checks**, including a typed producer/consumer queue,
enable/disable, replacement, failure/restoration, cleanup retry and six repeated
replacement cycles. See
[the new board record](../../qualification/esp32c3-activation-20260928.json).
Historical qualification records remain unchanged.

The board now runs this acceptance image. Checkpoint 3 integrates the I2C/BMP388
adapter; its physical module, supply, SDA/SCL wiring and address must be confirmed
for hardware acceptance. Streaming, complete target descriptor/wiring admission,
USB configuration, persistence and real peripheral recovery remain outstanding.
The target peripheral list remains empty and AI stays deferred.

## Checkpoint 3 software progress

The BMP388 registry adapter now uses the existing periodic activation and shared
queue/health APIs. No sensor-specific lifecycle branch was added to this module.
The reusable I2C backend owns one controller; sensor contexts borrow the bus and
claim their addresses. Host tests exercise two instances, copied measurements,
fault records and retryable sensor cleanup through the real activation manager.
See [the driver record](../../drivers/bmp388/INTEGRATION.md).
Physical acceptance awaits the available module's wiring; AI remains deferred.

## SPI/OLED software checkpoint

SPI2, shared C3 pin ownership and the OLED registry adapter now compile with this
runtime. The runtime contains no device-specific branches. The host fixture
exercises independent frames, transport retry and retained cleanup. See the
[SPI integration guide](../../drivers/ssd1306_spi/INTEGRATION.md). Hardware checks
are deferred to final acceptance; the prior activation image remains the latest
physically qualified board build.

## GPIO distance/output software checkpoint

HC-SR04 uses the existing periodic worker, registry and shared distance queue;
host tests cover independent instances and retained cleanup/recovery. Generic
PWM exposes component writes and explicit cleanup for a later command owner.
Runtime code remains independent of protocol names. See
[GPIO review and deferred hardware tests](../../drivers/hc_sr04/INTEGRATION.md).
The next checkpoint adds target configuration transfer/admission/storage.

## Checkpoint 6a observation update

The existing shared guard now also protects copied latest samples and consistent
snapshots. The application owner checks registered heartbeat deadlines; it does
not restart workers or infer sensor health from task progress. Runtime lifecycle
remains independent of equipment. See [the service contract](../services/TELEMETRY.md).
The earlier board result remains the latest physical qualification.
