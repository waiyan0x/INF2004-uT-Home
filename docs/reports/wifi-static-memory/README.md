# Wi-Fi static memory and resource assessment

## Current network resource assessment

**The transaction image passes fixed-linker budgets but does not satisfy the
whole-system no-application-heap requirement yet.** This assessment covers the
actual Wi-Fi/MQTT configuration application. MQTT and HTTPS setup must remain
available together, as confirmed by the user; USB setup/recovery remains too.
The contemplated Wi-Fi-driver exception does not automatically exempt TLS,
TCP/IP or ESP-TLS contexts.

The current build uses pinned ESP-IDF 6.1
`fff9895c82d744c7237be8847347bdd1b07c6643`, kernel
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` and the maintained owned patches.
The 15 November fixed-lwIP-record build used the existing private bench settings;
no board was flashed. Local generated evidence is in `build/lwip-records/` and
the isolated build directory `C:/Users/kkak4/esp/uthome-lwip-records-20261115b/`.
The target's `evidence.json` SHA-256 is
`02106a968b0cc9771a2117cd7fd91774120bce1a458b05b1fb04bb9bb5b5d547`.
The application image SHA-256 is
`19eefd77cb0a39abf4112bf3134245eddc41c9ff5ef095399aa61971ce39f897`.
Source-snapshot verification passes against the original build inputs.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 131,624 | 147,456 | 15,832 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,237 | 55,296 | 1,059 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 260,746 | 278,528 | 17,782 |
| RTC, separate from DRAM | 60 | 128 | 68 |

These are linker measurements, **not runtime free heap, task stack margins or
ESP timing**. The published mailbox baseline (`eede615`) used 275,930 fixed DRAM
bytes. Compact configuration views saved 24,600 bytes; fixed ESP-TLS and lwIP
records reserve part of that reclaimed capacity. The current image is
**15,184 bytes smaller than that baseline**. All aggregate caps are unchanged.
MQTT remains 1,816/1,824 bytes and HTTPS 3,098/3,104; future network storage needs
an explicit module reservation within the application cap.

### Reclaimed configuration storage

`cfgjson` now reads the caller's existing configuration bytes through 512 typed,
eight-byte views instead of retaining a copied cJSON tree/text. The target module
uses 4,117 bytes including bookkeeping, with its transaction reservation reduced
to 4,128. The unlinked cJSON mutable-storage rows are removed from current budget
policies; the pinned vendor source remains unchanged. The profile schema and
admission limits remain intact. Input is borrowed until close; production
plan/discovery callers finish reading and copy scalar settings before returning.
See the [contract and manual review](../../../firmware/src/config/README.md#admission-c-review).

Reader tests include 500 generated nested documents and 2,000 grammar
mutations checked against an independent Python decoder, plus explicit numeric,
string, capacity, depth and lifetime cases. Actual owned C is compiled with
warnings as errors; allocator interception verifies no reader allocation calls.
They remain part of the full target-facing regression suite.

### Fixed ESP-TLS records

`c3tls` uses 9,054 fixed bytes within a 9,056-byte module reservation: three
typed ESP-TLS contexts (8,736 bytes), three error records (60), temporary hostname
storage (254) and four lease flags. This preserves one MQTT plus two HTTPS
connections. The total DRAM increase is 9,056 bytes including linker padding.
These bytes are now budgeted before runtime; this is a change in allocation
strategy, not a claim that secure connections need less total memory.

Only the two selected ESP-TLS compilation units bind calloc/free/strndup to the
owned stores. Four error-tracker functions use fixed records with the SDK's
public error layout. Unsupported requests or exhausted stores fail without
heap fallback. The target gate verifies exact source scope, all linked aliases,
absence of the original allocating error tracker, flash placement and continued
use of SDK connection setup/cleanup. Vendor source and certificate checks remain
unchanged. See the [contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-esp-tls-records).

Six direct adapter tests cover zeroing/alignment, independent error categories,
temporary-name capacity, exhaustion, initialization-failure cleanup and 1,000
full lease cycles with allocator interception. A qualification regression attacks
missing, duplicated and escaped bindings and invalid linked symbols. These tests
exercise records and build enforcement; they do not perform a TLS handshake.

Earlier ESP-TLS record implementation verification: **490 target-facing tests and 56 C-policy tests passed**.
The full 329-file pinned C gate, manual review, warnings-as-errors target build,
exact-source snapshot verification and every fixed-memory budget also pass.
Those dummy-settings build reports are in `build/tls-records/` and
`C:/Users/kkak4/esp/uthome-tls-records-20261114/`; that build's unchanged
`evidence.json` SHA-256 is
`08b8fdbe07b0dd4944aa2ac1edc1b2aa00080cd240d56cc0821dc746c6a81e67`.
Previous foundation
and runtime results remain in `build/compact-json/`; those unaffected suites were
not rerun for this SDK-record change.

### Fixed lwIP mailboxes

`c3lbox` replaces the allocating/freeing SDK mailbox functions and uses the
existing eight shared queue slots. Target-link checks confirm both aliases and
absence of the original `sys_mbox_new`/`sys_mbox_free`. Eight distinct typed
identities use 672 flash bytes, plus a 32-byte pointer table. The adapter adds
**zero mutable storage** and no extra native objects. Its entry points and
queue-binding helpers are flash code. The number of live mailboxes has not been
measured, so this does not quantify runtime heap savings.

Focused tests cover 1,000 create/free cycles, shared exhaustion, exact-capacity
and invalid requests, zero-valued messages, ISR posting and nonempty-close
preservation. See the
[adapter contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-lwip-mailboxes).
This closes mailbox backing for selected Wi-Fi service builds; broader client
overlap and the new image's live-network validation still require the board.

### Fixed lwIP connection and datagram records

`c3lrec` uses **352 bytes** within its own 352-byte application reservation:
six 52-byte `struct netconn` objects, two 16-byte `struct netbuf` objects and
eight one-byte lease flags. Only this owned object adds writable object storage
relative to the earlier working Wi-Fi image. Overall linked DRAM grows by 360
bytes; RAM-code and task-pool usage are unchanged. Aggregate caps are unchanged.

Source-scoped replacements cover all NETCONN/NETBUF allocations and releases in
the pinned `api_lib.c`, `api_msg.c` and `netbuf.c`. SDK protocol and cleanup code
remain intact. The adapter returns NULL at exhaustion and uses the existing
native interrupt boundary for shared access. Counts include listeners and UDP
sockets, not just secure clients. There is no added task, mutex or heap fallback.
See the [contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-lwip-records).

**524 target-facing tests passed**, including four direct record tests for
independent capacity, zeroing/alignment, invalid releases, interrupt restoration
and 1,000 reuse cycles with allocator interception. The full **333-file C gate**,
manual review, warnings-as-errors target build, original-source verification and
every module/group budget pass. The first target link exposed archive ordering;
the final build explicitly retains the adapter and native exclusion functions.
All **46 qualification tests** passed again after that correction. Target
qualification checks the three exact substitution scopes, linked flash helpers
and SDK lifecycle; the generic allocator remains for other lwIP pools.

The new image is **unflashed**. The earlier working image remains on the ESP;
the configured hotspot was not visible during this step. Live reconnect,
overlapping MQTT/HTTPS clients, pool-exhaustion recovery and interrupt latency
remain unverified for this change. Earlier [Wi-Fi/MQTT hardware results](../../../firmware/src/network/README.md#live-mqtt-check)
do not qualify this new allocation path. Packet buffers, protocol control blocks
and TLS state still allocate from the heap.

### Reachable allocation paths

The following paths were checked in the pinned SDK source and selected compiler
commands. The ELF still links `esp_tls_init`, `mbedtls_ssl_setup`,
`esp_mbedtls_mem_calloc`, `netconn_alloc` and `memp_malloc`. The mbedTLS/lwIP
units retain their original allocators outside the three record-adapted API
units. ESP-TLS records/names, lwIP connection/datagram records and mailbox
backing use owned stores. Linking an allocator alone
would not prove reachability;
the owned entry paths below establish application use.

| Area | Source path and behavior | Assessment |
|---|---|---|
| Decision, codec and MQTT packet storage | `txjob`, `txport`, `c3tx`, `netmqtt`, `netbroker`, `c3mqtt`: fixed owned state and bounded copied buffers | No application allocator/object path; reviewed and software-tested |
| Native task stacks/notifications | `c3task` and the owned compatibility patch reserve/seal startup slots | Software and target-link checks pass; runtime stack/creation counters need the board |
| Native queues/semaphores/mutexes/event groups | `c3nq`/`c3event` pre-create native objects; SDK create/delete APIs lease/release fixed records | Bounded exhaustion/reuse; does not cover surrounding SDK heap storage |
| ESP-TLS contexts, error records and temporary names | `c3mqtt`/`c3https` still call the SDK lifecycle; two SDK units bind allocation to `c3tls`, and error trackers use linked fixed replacements | Context/name allocation is bounded for the selected three-session path; live handshake/reconnect acceptance remains |
| MQTT and HTTPS encryption state | After fixed context admission, SDK setup enters mbedTLS for certificates, keys, persistent TLS hostnames and record buffers | Still heap-backed on first connection/reconnect; outside the contemplated Wi-Fi-driver exception |
| TLS record buffers | `ssl_tls.c` allocates input/output buffers; `port/esp_mem.c` routes calloc to internal heap | Internal allocator, 16,384-byte input and 4,096-byte output content lengths; dynamic-buffer option off. This option does not eliminate allocation |
| lwIP connection/datagram records | Three API units bind NETCONN/NETBUF allocation/free to typed `c3lrec` stores | Six connections/two envelopes; software and target proof pass, live acceptance pending |
| TCP/IP packet and control-block pools | `lwipopts.h` still sets `MEM_LIBC_MALLOC=1`, `MEMP_MEM_MALLOC=1`; other units keep SDK allocators | Application network activity still reaches general heap-backed pools |
| lwIP mailbox backing | Wi-Fi builds link `c3lbox`; messages use existing `c3nq` payloads | Bounded software/target-link proof passes; shared capacity needs live acceptance |
| Wi-Fi/SDK startup and events | `c3wsta` creates the network interface/event loop and registers callbacks; radio code uses SDK allocations | Inventory and justify any narrow driver exception; do not exempt all SDK networking by implication |

### TLS capacity constraint

A standalone sizing object compiled with the selected RV32 target configuration
reports the following. It was not linked into firmware or run on the board.
Local probe source, compiler arguments and results are in `build/compact-json/`.

| Selected target type or macro | Bytes |
|---|---:|
| `sizeof(esp_tls_t)` | 2,912 |
| `sizeof(mbedtls_ssl_context)`; already included above | 264 |
| `sizeof(mbedtls_ssl_handshake_params)` | 2,448 |
| `MBEDTLS_SSL_IN_BUFFER_LEN` | 16,717 |
| `MBEDTLS_SSL_OUT_BUFFER_LEN` | 4,429 |

The context is already counted in `c3tls`; do not reserve it twice. Each remaining
record-buffer pair needs **21,146 bytes**, before certificates, keys, transforms
and handshake allocations. Even one pair exceeds the current 15,832-byte application
headroom; two pairs need 42,292 bytes and three need 63,438. These are compile-time
capacity observations with the current buffer lifetimes, not measured peaks.

The next design must account for buffer and handshake lifetimes as well as
bounded allocation. The SDK's dynamic-record-buffer mode is unsuitable as-is
for the nonblocking application, as the probe below demonstrates. No mbedTLS
allocator binding or connection-policy change is implemented here.

### Dynamic TLS buffer assessment

An isolated experiment based on `23f1f267d6afeff25c53ce370c8fd78c17ccd5c2`
enabled only `CONFIG_MBEDTLS_DYNAMIC_BUFFER` in the network defaults. The real
RV32 image built successfully but **failed the fragmented-header software probe**.
The working application keeps the option off. Its CMake guard now rejects
enabling it; linked-symbol qualification independently rejects the dynamic
receive helper. SDK source and pins remain unchanged.
The experimental map also exceeds the existing `c3tls` module reservation
(9,066/9,056 bytes); that failed budget was retained without changing the cap.

In the pinned `components/mbedtls/port/dynamic/esp_mbedtls_dynamic_impl.c`,
`esp_mbedtls_add_rx_buffer` points `in_hdr` at a five-byte local stack array.
`mbedtls_ssl_fetch_input` can receive part of the header and return `WANT_READ`.
The byte count survives, but the array's lifetime ends. When the owner returns
later, the SDK reads only the missing bytes into a new local array. The read
wrapper returns this retry status directly; it does not save the partial header.

The [reproducer](../../../tools/target/tls_fragment_probe.py) executes both actual
compiled RV32 functions. Transport, allocation and ROM byte-copy operations are
simulated; the returned stack is overwritten between calls to represent valid
intervening owner work. Field offsets come from the ELF's DWARF, not host type
sizes. It exercises a five-byte header `16 03 03 00 06`:

| Delivery | Observed header after retry | Allocation requested |
|---|---|---:|
| All five bytes together, control case | `16 03 03 00 06` | 347 bytes |
| Split after byte 1, 2 or 3 | Earlier bytes replaced by `A5` | 347 bytes |
| Split after byte 4 | `A5 A5 A5 A5 06` | 42,587 bytes |

All four split cases reproduce loss; the last also shows that corrupted length
bytes reach allocation before later TLS validation. This is an instruction-level
software result, **not a live TLS handshake, board observation, security exploit
claim or timing measurement**. It is sufficient to reject this option for the
current owner loop. It does not establish behavior for every SDK release.

Local experiment: `C:/Users/kkak4/esp/uthome-tls-dynamic-probe-20261114/`.
Its ELF SHA-256 is
`4b540ae35991bff7e306b8c7c3d8cb82bc129cf15566400dff3f6f07ac73cffc`.
The report and logs are under ignored `build/tls-capacity/`; no image was flashed.
Use the [target tool procedure](../../../tools/target/README.md#tls-fragment-reproducer)
to repeat the probe. The tool's successful exit means the defect was reproduced,
not that TLS acceptance passed.

For the build guard, **51 affected qualification/TLS-record tests passed**, including
execution of the CMake rejection and linked-symbol rejection paths. The pinned
329-file C gate passes. A fresh normal target build with warnings as errors and
snapshot verification passes at the unchanged **260,386/278,528 DRAM bytes**;
every module and aggregate budget passes. No project C or header was changed.
That build is `C:/Users/kkak4/esp/uthome-tls-buffer-guard-20261114/`, with
`evidence.json` SHA-256
`1e6e49853fcc6f10628fa0a63eea0e8a80837d9fec1fd5c709a8cf2f99c2cb41`.
Logs, memory report and the five-case reproducer report are in
`build/tls-capacity/`. This is software/build validation, not live acceptance.

A safe smaller-buffer design must retain partial headers and records across
retries, validate lengths before requesting storage, and preserve unread data
for each live connection. One shared scratch array cannot replace storage still
owned by another connection. Capacity must cover admitted handshake/record
overlap with deterministic exhaustion and cleanup; it must fit the unchanged
budgets alongside certificates, keys and TCP/IP storage. Default record buffers
still exceed the available application headroom, so this assessment closes the
SDK-option investigation, **not the network allocation blocker**.

### Required closure before whole-system acceptance

1. Provide bounded storage for the remaining mbedTLS and reachable lwIP
   allocations, including retained TLS hostname copies. Establish lifetime,
   concurrency, exhaustion, alignment and cleanup behavior before assigning
   capacities. Preserve vendor pins and all aggregate budget caps.
2. Preserve simultaneous MQTT and HTTPS setup plus USB recovery. Budget their
   overlapping lifetimes; reducing HTTPS client count alone does not solve the
   TLS storage issue. Keep certificate and server-name checks intact.
3. Test exhaustion and reconnect against the adapted real SDK paths, then rebuild
   and check every module and aggregate reservation. A fixed arena with unbounded
   fallback is not acceptable.
4. On the selected board, capture `c3audit`/task counters and stack headroom at
   startup, first TLS connection, repeated reconnect and concurrent permitted
   peripheral/configuration traffic. Verify no post-startup native object
   creation and classify every remaining heap event under an explicit policy.

This is not completed resource acceptance. Earlier live MQTT/BMP388 checks and
allocation-event counts are recorded in the network guide; they neither measure
heap peak nor qualify the new lwIP-record image. Runtime stack headroom, wider
secure-connection overlap and Home Assistant acceptance remain open. The
persistent host controller is already implemented and software-tested.

For reproduction, use the [target build guide](../../../tools/target/README.md)
with `--runtime-port --configuration-app --static-net-queues --network-services wifi`
and private build settings, then the
[transaction policy](../../../tools/target/transaction-budget.json):

```powershell
python tools/target/memory_budget.py --build ABSOLUTE_FRESH_WORK_DIR --budget tools/target/transaction-budget.json --output build/tls-records/memory-new.json
python tools/target/qualify_kernel.py verify-snapshot --project-dir ABSOLUTE_FRESH_WORK_DIR/project
python tools/c_standard/check.py
python -B -m unittest discover -s tests/target -v
python -B -m unittest discover -s tests/c_standard -v
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
```

The selected toolchain, test dependencies and pinned formatter must be active.
Keep generated reports and private provisioning inputs under ignored `build/`;
leave the original source snapshot unchanged when verifying evidence.

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
