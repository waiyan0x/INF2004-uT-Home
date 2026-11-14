# Wi-Fi static memory and resource assessment

## Current network resource assessment

**The transaction image passes fixed-linker budgets but does not satisfy the
whole-system no-application-heap requirement yet.** This assessment covers the
actual Wi-Fi/MQTT configuration application, not just the isolated station
fixture described below. The source requirement includes network communication;
the contemplated Wi-Fi-driver exception does not automatically exempt TLS,
TCP/IP, ESP-TLS contexts or SDK mailboxes.

The 14 November 2026 session build used pinned ESP-IDF 6.1
`fff9895c82d744c7237be8847347bdd1b07c6643`, kernel
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` and the maintained owned patches.
Build-only dummy settings were used; no board was flashed and no live broker was
contacted. Local generated evidence is in `build/step1-verification/` and the
isolated build directory `C:/Users/kkak4/esp/uthome-session-mqtt-20261114/`.
The target's `evidence.json` SHA-256 is
`bd746cb966b0623b028825cbeb1f1bb276807090a1868e4b27895831df48a0e8`.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 146,819 | 147,456 | 637 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,226 | 55,296 | 1,070 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 275,930 | 278,528 | 2,598 |
| RTC, separate from DRAM | 60 | 128 | 68 |

The transaction owner uses 817/880 bytes, down 24 bytes from the cleanup build.
Its linked decision state is 448 bytes and bus-adapter state 168 bytes. MQTT is
still 1,816/1,824 bytes, HTTPS 3,098/3,104, and network metadata 84/96. These are
map measurements, **not runtime free heap, task stack margins or ESP timing**.
The decision path adds no heap, task or kernel object. Its work is bounded by
one 96-byte command/reply/cache, a 16-byte name and 30 TX / 32 RX bytes. Each
owner tick advances at most one driver poll; a status query does not poll.

### Reachable allocation paths

The following paths were checked in the pinned SDK source and selected compiler
commands. The fresh ELF links `esp_tls_init`, `mbedtls_ssl_setup`,
`esp_mbedtls_mem_calloc`, `netconn_alloc`, `memp_malloc` and `sys_mbox_new`.
Those SDK compilation units have no allocator-replacement definitions. Merely
linking an allocator would not prove reachability; the owned call sites below
are the entry paths.

| Area | Source path and behavior | Assessment |
|---|---|---|
| Decision, codec and MQTT packet storage | `txjob`, `txport`, `c3tx`, `netmqtt`, `netbroker`, `c3mqtt`: fixed owned state and bounded copied buffers | No new application allocator/object path in this change; reviewed and software-tested |
| Native task stacks/notifications | `c3task` plus the owned task compatibility patch reserve/seal startup slots | Software and target-link checks pass; runtime stack/creation counters still need the selected board |
| Native queues/semaphores/mutexes/event groups | `c3nq`/`c3event` pre-create native objects; SDK create/delete APIs lease/release fixed records | Lease reuse does not mean new native objects; exhaustion is bounded. This does not cover the SDK's surrounding heap storage |
| MQTT TLS | `c3mqtt` connection callback calls `esp_tls_init()`; `components/esp-tls/esp_tls.c` allocates its context with `calloc`; async setup enters mbedTLS | Reachable on first connection and reconnect; violates the current no-heap objective outside any approved exception |
| HTTPS TLS | `c3https` accepts up to two clients and creates ESP-TLS sessions at runtime | Adds the same context/certificate/record-buffer allocation issue; limits on client count alone do not make it static |
| TLS record buffers | `components/mbedtls/mbedtls/library/ssl_tls.c`, `mbedtls_ssl_setup()` allocates input/output buffers; `port/esp_mem.c` routes calloc to internal heap | Selected config: internal allocator, 16,384-byte input and 4,096-byte output content lengths, dynamic-buffer option off. Turning that option off does **not** remove heap allocation |
| TCP/IP connections and packets | `components/lwip/port/include/lwipopts.h` sets `MEM_LIBC_MALLOC=1`, `MEMP_MEM_MALLOC=1`; `api/api_msg.c` allocates netconn records and receive mailboxes for connections | Application network activity reaches general heap-backed pools; not just a Wi-Fi-driver implementation detail |
| lwIP mailbox backing | `components/lwip/port/freertos/sys_arch.c::sys_mbox_new()` calls `heap_caps_malloc` before `xQueueCreateStatic` | Fixed native queue adaptation does not eliminate this separate allocation |
| Wi-Fi/SDK startup and events | `c3wsta` creates the network interface/default event loop and registers callbacks; the radio uses its SDK allocations | Inventory and explicitly justify any narrow driver exception. Do not extend it to all SDK networking by implication |

One TLS connection's configured record-content capacities alone total 20,480
bytes, before context, certificate and handshake overhead. They cannot simply
be moved into the current 2,598-byte total headroom. Three simultaneous TLS
sessions are possible (one MQTT plus two HTTPS); this is a source capacity
observation, not a measured peak or an allocation reservation.

### Required closure before whole-system acceptance

1. Reclaim and explicitly budget storage, then provide bounded owned adapters or
   scoped build-time allocator bindings for ESP-TLS/mbedTLS and the reachable
   lwIP allocations. Include lifetime, concurrency, exhaustion, alignment and
   cleanup behavior. Preserve pinned vendor source and all existing budget caps.
2. Review connection counts and optional HTTPS setup/recovery duties as part of
   that design. Reducing HTTPS clients alone does not solve MQTT's allocation
   paths. Do not disable TLS authentication or weaken the memory requirement to
   obtain a passing result.
3. Run exhaustion and reconnect tests against the adapted real SDK paths, then
   rebuild and check every module and aggregate reservation. A fixed arena with
   hidden unbounded fallback is not an acceptable replacement.
4. On the selected board, capture `c3audit`/task counters and stack headroom at
   startup, initial TLS connection, repeated reconnect and concurrent permitted
   peripheral/configuration traffic. Verify no post-startup native object
   creation and classify every remaining heap event under an explicit policy.

This is a concrete blocker assessment, not completed resource acceptance.
No runtime heap/stack measurement, live MQTT, hardware bus or Home Assistant
acceptance is claimed. The next implementation priority is the bounded network
allocation design; the persistent host controller follows the session contract.

For reproduction, use the [target build guide](../../../tools/target/README.md)
and [transaction policy](../../../tools/target/transaction-budget.json):

```powershell
python tools/target/memory_budget.py --build ABSOLUTE_FRESH_BUILD --budget tools/target/transaction-budget.json --output build/step1-verification/memory.json
python tools/target/qualify_kernel.py verify-snapshot --project-dir ABSOLUTE_FRESH_BUILD/project
```

The selected toolchain and pinned formatter must be active. Keep generated
reports and private provisioning inputs under ignored `build/`; the original
source snapshot must remain unchanged when verifying evidence.

## Existing task-stack and lifecycle implementation

The sections below describe the earlier isolated task-pool work. Their numbers
belong to those original profiles; the current transaction-image table above is
the applicable integration budget.

## Outcome and scope

The isolated ESP32-C3 Wi-Fi and BLE diagnostics create shim-managed native tasks
from bounded, caller-owned stacks. The staged compatibility patch contains no
task-stack `heap_caps_malloc`/`heap_caps_free` path, uses generation-tagged opaque
handles, and defers unsafe cleanup to one permanent low-priority reaper. Native
task and notification-semaphore creation is now a startup-only operation: each
reservation is committed after both native objects exist, and the radio probe
seals the pool after its first successful scan or advertising checkpoint. Later
creation fails before either native creation call. The pinned
`firmware/third_party/esp32c3-mtk3` checkout remains unchanged.

The production patch also implements the selected one-entry FreeRTOS task
notification contract. Notification value and pending state are authoritative;
the bounded native semaphore is only a wake hint. Set-bits, increment, overwrite,
no-overwrite, no-action, clear-on-entry/exit, blocking/timed waits, take,
state/value clear, ISR variants, stale handles and invalid indexes have direct
tests against the patched source.

This closes the software implementation, host-test and target-build portion of
the Buddy 1 task/notification gap. No board was flashed. On-device stack margins,
radio CPU/timing results, cleanup fault injection and whole-system static
compliance remain unqualified.

## Design

`c3task` owns mutually exclusive fixed profiles selected at build time:

| Profile | Native task stack reservations | Pool total |
|---|---|---:|
| Wi-Fi | event 2,816; TCP/IP 3,584; ESP timer 4,096; Wi-Fi 6,656 bytes | 17,152 bytes |
| BLE | ESP timer 4,096; NimBLE host 4,096; controller 4,096 bytes | 12,288 bytes |

All slots are 16-byte aligned. A bounded best-fit search rejects zero, oversize
and exhausted requests without heap fallback. Ownership includes the handle
generation; release requires the matching base pointer, capacity and generation.
The pinned native port already reserves a separate 4 KiB interrupt stack, so no
per-task interrupt margin is duplicated.

The shim has four lifecycle records, matching the largest profile. Its ordered
states are reserved, running, stopping, termination pending, waiting dormant,
task deleted, semaphore deleted and stack released. Creation rollback enters the
same cleanup sequence. Self-delete only publishes termination; external and
concurrent deletion requests converge on the same record. Stale generation handles
cannot name a reused slot.

Cleanup runs in a permanent 1,024-byte reaper task at native priority 31, below
radio work and immediately above idle priority 32. It wakes every 100 ms and
attempts at most four records per pass. Failed task deletion, semaphore deletion,
stack release or observer release retains the record at the failed state for a
later retry. A stack is never returned while its task may still execute.

`c3task_seal()` succeeds only after the reaper is ready and every profile slot is
occupied and committed. The acquire/seal decision is protected by the existing
single-core interrupt guard. A direct post-seal acquisition test verifies that
pool occupancy is unchanged and the rejection counter advances. Current Wi-Fi
stop/start and BLE advertising stop/restart retain their native task and
notification objects, so their required diagnostic recovery remains supported.
Full `esp_wifi_deinit()`/reinitialization and NimBLE host/controller teardown
followed by reinitialization would recreate native tasks and are intentionally
unsupported after the seal. A future service needing another native task must
reserve it before sealing or revise the reviewed profile and budget.

## Verification

- The actual `c3task.c` allocator passed alignment, best-fit, exhaustion,
  ownership/stale-release and 10,000-cycle tests under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The real staged shim patch passed creation rollback, pool exhaustion, task/
  semaphore/stack/observer cleanup failure, self/concurrent/stale deletion and
  1,000 reuse cycles under the same sanitizers. Leak detection was disabled
  because the macOS sanitizer runtime does not implement it.
- The notification harness applies the production patch to a pristine pinned
  shim and compiles the extracted production implementation with AddressSanitizer
  and UndefinedBehaviorSanitizer. It passed set-bits, increment, overwrite,
  no-overwrite, pending-state, entry/exit clear, state/value clear, finite timeout,
  stale wake hint, ISR, invalid-index and stale-handle cases. Final Wi-Fi and BLE
  ELFs link `vTaskGenericNotifyGiveFromISR`, `ulTaskNotifyTake` and
  `ulTaskGenericNotifyTake`; the remaining implemented entry points are discarded
  from these two selected links but remain patch-level tested.
- Legacy reservation, failure, self-delete and concurrent-delete regressions,
  seven fixed-pool unit tests and the 1,000-cycle model stress test passed.
- The complete BARR-C formatter/lexical gate passed 285 project C files with the
  pinned clang-format 20.1.8. The 54 checker unit tests passed. Manual review found
  no new deviation: fixed widths and bounds are explicit; shared lifecycle state
  uses the existing single-core interrupt guard; no ISR blocks; pointer values are
  compared only; and the 32-bit generation token is confined to the target handle
  ABI. Both real target builds compile owned code with warnings as errors.
- The staged patch applies cleanly to the pinned vendor source. Its SHA-256 is
  `402dc6cb3325fc1352be7e9b6fe8fa2557edc3be308498195bf5dc8c7ebb69c2`.

Final unflashed build evidence:

| Build | Evidence | Result |
|---|---|---|
| Wi-Fi scan/restart | `/private/tmp/uthome_buddy1_accept7b_wifi/evidence.json`; `memory-budget.json` | build and budget passed; commit/seal/snapshot/static-stack/reaper symbols linked |
| BLE advertising | `/private/tmp/uthome_buddy1_accept7b_ble/evidence.json`; `memory-budget.json` | build and budget passed; commit/seal/snapshot/static-stack/reaper symbols linked |

The qualification tool verifies the patched source, rejects task-stack heap calls,
checks the static APIs and lifecycle tokens, and confirms the final ELF contains
the allocator, reaper stack/task and shim cleanup function. These local build
folders are evidence inputs, not installed firmware or hardware observations.

## Memory impact

The fixed stacks move radio task memory into the ELF. Historical pre-change maps
excluded dynamically allocated task stacks and therefore are not equivalent
runtime-peak measurements.

| Profile | Historical ELF DRAM | Previous qualified DRAM | Current final DRAM | Change from historical | Final cap headroom |
|---|---:|---:|---:|---:|---:|
| Wi-Fi | 262,106 | 278,402 | 278,354 | +16,248 | 174 bytes |
| BLE | 247,703 | 259,127 | 259,063 | +11,360 | 19,465 bytes |

For Wi-Fi, the current group attribution is 168,046 bytes application fixed,
53,170 SDK/kernel fixed and 57,138 RAM code. Its subgroup margins are 146, 14 and
14 bytes. For BLE the current attribution is 163,181, 39,544 and 56,338 bytes.
`network-budget.json` retains the 278,528-byte total and the 168,192 application,
53,184 SDK/kernel and 57,152 RAM-code limits. The previously reported 126-byte
margin came directly from the preceding qualified Wi-Fi map:
`278,528 - 278,402 = 126`; it was aggregate ELF headroom, not measured runtime
free heap. The current 174-byte aggregate fit is still not comfortable growth
capacity. The detailed stack table is in
[wireless_stack_budget.csv](wireless_stack_budget.csv).

## Hardware work still required

On a deliberately selected board and only after backup/identity checks, build and
flash fresh diagnostic images separately. For each profile:

1. Confirm every requested task maps to its expected slot and every used slot is
   committed; `unused` must be nonzero for every active radio task and the reaper.
2. Require `post_seal_task_creates=0`,
   `post_seal_notification_sem_creates=0`, `sealed=1`, and a post-seal rejection
   increment without an occupancy change.
3. Capture the radio window's elapsed, idle, busy and busy-permille values. Confirm
   scan/advertising completes, stop/restart completes, and the same checks pass
   after reset. These measurements must be recorded; no host value substitutes.
4. Record task observer creation/deletion/peak data and cleanup request/completion,
   latency, retry/failure and reaper-call/headroom data. Normal radio restart may
   legitimately have zero cleanup requests because it reuses the existing tasks.
5. Use a separately reviewed fault-injection fixture before claiming on-board
   rollback, deletion-retry or reclaimed-stack recovery; the current board image
   observes those counters but does not deliberately corrupt a live radio task.
6. Repeat under combined peripheral/radio traffic, Wi-Fi/BLE coexistence and
   flash-save stress before any integrated or whole-system claim. Continue the
   SDK allocation inventory; this task-stack result grants no heap exemption.

Exact build, flash, monitor and log acceptance commands are in the
[runtime fixture guide](../../../tests/target/runtime_port_board/README.md#buddy-1-radio-resource-acceptance).
The 174-byte Wi-Fi aggregate margin, and especially the 14-byte SDK/RAM-code
subgroup margins, are the primary build-time risk. Any source, SDK configuration
or toolchain change requires a fresh map and budget record.
