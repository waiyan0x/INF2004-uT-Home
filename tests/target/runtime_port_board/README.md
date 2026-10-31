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
native UART fault acceptance. An attached radar invalidates the no-external-sender
assumption; preserve failed captures instead of treating them as acceptance.

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
`alive` counters. A failed check
prints its expression and line, then keeps the board alive without proceeding.
Preserve the log and repeat after reset. Startup-create and binding/state-query failure injection
and the full 16 KiB budget rejection are checked by the separate native fake-
kernel suite; do not describe those failures as physically induced on the board.

The test uses permanent workers and memory-only fixture callbacks. Kernel pool
size depends on the selected build; inspect its map rather than assuming 32 KiB.
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

The separate `--senlux-probe` image checks the known BMP388 chip-ID register at
0x77 before starting the fixed SEN0390 GPIO-enable/read sequence. It logs the
preflight result and every light-read status without changing saved profiles.
The preflight uses initialized one-byte buffers, a single 100 ms deadline and a
synchronous, serialized read. It introduces no task, heap allocation or shared
buffer lifetime. This is an isolated diagnostic, not four-device acceptance.

`--senlux-app-probe` is a separate diagnostic configuration application. It
enables the provisional light binding only for that build; use a full flash
backup because applying the profile updates saved configuration. The host suite
also compiles this explicit mode and checks four-worker activation, failed GPIO
acquisition, light and fault samples, retained cleanup and recovery.

## Peripheral and configuration coverage

The fixture links I2C/SPI/UART/GPIO drivers and configuration/admission modules
with bounded no-I/O checks. These do not establish peripheral communication or
electrical behavior. Startup return-value/idempotence checks are distinct from
counting kernel creation calls. Use the application and hardware procedures for
sensor/display communication, persistence and outputs; use the isolated UART
fixture for injected ring faults. Historical incremental check counts are in
the [completed-work log](../../../docs/COMPLETED_WORK_LOG.md).

## Current core and radio diagnostic scope

The current isolated core fixture additionally checks sampled native scheduler
time: a 100 ms delay advances elapsed/idle counters with a one-ms tick. Native
stack checks run after quiescence; unavailable live worker margins remain null.
The evidence index records tested image identities; query the actual board
and confirm serial ownership before a new capture.

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

## Model comparison with active peripherals

The separate `--model-load-probe` mode retains the configuration application
and runs the varied-goal classifier during explicit workload observations.
Use it with `--runtime-port --configuration-app --static-net-queues`; see the
[loaded diagnostic procedure](../../../tools/target/README.md#loaded-classifier-diagnostic)
for phase timing and its diagnostic-only reply contract. `loadmodel/fixture.json`
binds `ailite`, the immutable `aivects` corpus and the `aiload` observer wrapper.
Ordinary application builds must exclude all four diagnostic entry points.

Use one serial owner and a sustainable combined-observer cadence. Keep the first
settling period of each phase out of throughput comparisons. Worker stack
sentinels are measured only after cooperative stop; retain null live readings
and capture dormant margins after the timed windows, then restore the original
profile and firmware. Preserve incomplete attempts and any procedure correction.
This checks classifier load, not feature construction or full advice latency.

Manual BARR-C review: the observer belongs to the existing serialized controller,
uses fixed scalars, bounded four-call batches and a validated immutable tree.
Time is checked before narrowing, cumulative counts cannot wrap within an
accepted batch, and indices stay inside the frozen corpus. Replies use explicit
word encoding and retain invalid-input checks. No allocator, task, lock, runtime
model parser, Apply operation or new compatibility exception is introduced.
