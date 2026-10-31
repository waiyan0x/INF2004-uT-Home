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

## Buddy 1 radio resource acceptance

The Wi-Fi and BLE fixtures now print a common `[radio-task]` resource record and
`[radio-probe]` allocation/CPU record. The task record includes each fixed stack's
capacity, measured unused sentinel bytes, committed state, observer creation and
deletion totals, post-seal rejections, cleanup latency/retries/failures, reaper
calls and reaper stack headroom. The allocation record separately reports native
task and notification-semaphore creates after the startup seal. Wi-Fi scan restart
and BLE advertising restart reuse their existing native tasks; full radio
deinitialization/reinitialization after sealing is outside this diagnostic's
supported recovery contract.

Run the following from the repository root in a fresh shell. The work directories
must not already exist. These commands build only; they do not access a board:

```sh
source "$HOME/esp/esp-idf/export.sh"
buddy1_run_root="$(mktemp -d /tmp/uthome-buddy1-radio.XXXXXX)"
buddy1_wifi="$buddy1_run_root/wifi"
buddy1_ble="$buddy1_run_root/ble"

python tools/target/qualify_kernel.py build \
  --sdk "$IDF_PATH" \
  --kernel firmware/third_party/esp32c3-mtk3 \
  --runtime-port --static-net-queues --wifi-probe \
  --work-dir "$buddy1_wifi" \
  --compat-patch tools/target/patches/freertos-systimer.patch \
  --observer-patch tools/target/patches/freertos-task-observer.patch
python tools/target/memory_budget.py \
  --build "$buddy1_wifi" \
  --output "$buddy1_wifi/memory-budget.json" \
  --budget tools/target/network-budget.json

python tools/target/qualify_kernel.py build \
  --sdk "$IDF_PATH" \
  --kernel firmware/third_party/esp32c3-mtk3 \
  --runtime-port --static-net-queues --ble-probe \
  --work-dir "$buddy1_ble" \
  --compat-patch tools/target/patches/freertos-systimer.patch \
  --observer-patch tools/target/patches/freertos-task-observer.patch
python tools/target/memory_budget.py \
  --build "$buddy1_ble" \
  --output "$buddy1_ble/memory-budget.json" \
  --budget tools/target/network-budget.json

python - "$buddy1_wifi" "$buddy1_ble" <<'PY'
import hashlib
import json
import pathlib
import sys

for root_text in sys.argv[1:]:
    root = pathlib.Path(root_text)
    evidence = json.loads((root / "evidence.json").read_text())
    for relative, expected in evidence["artifacts"].items():
        payload = (root / relative).read_bytes()
        assert len(payload) == expected["size_bytes"], relative
        assert hashlib.sha256(payload).hexdigest() == expected["sha256"], relative
    print(f"verified {root}")
PY
```

Before flashing, follow the project procedure to identify the current port,
confirm ESP32-C3/4 MB, verify the artifact hashes in `evidence.json`, and take a
hashed 4 MB backup. Do not substitute an old port or image. With `buddy1_port`
set to the identified port and all competing serial clients closed, the relevant
commands are:

```sh
python -m serial.tools.list_ports -v
buddy1_port=/dev/cu.usbmodemXXXX
python -m esptool --chip esp32c3 --port "$buddy1_port" flash-id
buddy1_backup="$buddy1_run_root/board-before.bin"
python -m esptool --chip esp32c3 --port "$buddy1_port" \
  read-flash 0x0 0x400000 "$buddy1_backup"
test "$(stat -f %z "$buddy1_backup")" -eq 4194304
shasum -a 256 "$buddy1_backup"

cd "$buddy1_wifi/build"
python -m esptool --chip esp32c3 --port "$buddy1_port" \
  --after watchdog-reset write-flash "@flash_args"
cd -
script -q "$buddy1_run_root/wifi-boot1.log" \
  python -m serial.tools.miniterm "$buddy1_port" 115200 --raw
```

Reset once while capturing the first image, then end miniterm with Control-].
Capture a second boot in `wifi-boot2.log`. Repeat the same flash and two-log
sequence with `$buddy1_ble/build`. The PowerShell equivalent, including artifact
hash verification and reviewed flash options, is in the
[target tool guide](../../../tools/target/README.md#install-the-configuration-application).

For each boot, preserve the full log and extract the diagnostic lines without
editing the capture:

```sh
rg '\[(wifi-probe|ble-probe|radio-probe|radio-task)\]' \
  "$buddy1_run_root"/*.log
```

The on-board acceptance checklist is:

- The selected probe prints `completed result=0`; continuing `alive` output is
  present, and the same result is obtained after reset.
- `post_seal_task_creates=0` and
  `post_seal_notification_sem_creates=0` on both boots.
- Every printed used slot has `committed=1` and nonzero `unused`; reaper
  `reaper_unused` is nonzero. Record the exact margins rather than imposing a
  new stack threshold without review.
- `sealed=1`; the deliberate post-seal exhaustion check increments
  `post_seal_rejections` and leaves the occupied count unchanged (a failed
  `CHECK` prevents a successful final report).
- CPU elapsed is nonzero, idle does not exceed elapsed, and busy permille is at
  most 1000. Compare both boots and retain the values; a build is not a CPU test.
- Observer requests/successes/deletions/active/peak and cleanup
  requests/completions/latency/retries/failures are retained. A normal restart
  can show no cleanup because the radio workers are reused.
- No `CHECK failed`, panic, watchdog reset, unexpected reboot or missing stage is
  present.

The fixture observes cleanup health and verifies sealed resource exhaustion, but
does not deliberately inject task-deletion or semaphore-deletion failures on a
physical radio stack. Host sanitizer tests cover those production-patch paths.
On-board cleanup retry and reclaimed-stack recovery remain **NOT TESTED** until a
separately reviewed fault-injection image is run.

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
