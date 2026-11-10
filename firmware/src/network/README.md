# Fixed network compatibility resources

This folder contains the owned queue, wait and resource-lease implementation
for the pinned ESP32-C3 networking compatibility layer. It is **experimental**:
the corrected SPI study includes the fixed core but does not activate radios.
Opt-in Wi-Fi/HTTPS, MQTT broker and BLE service bindings are implemented below.
Combined radio and whole-system static qualification remain open.

The SDK/kernel boundary modules live in the
[C3 network adapter folder](../../hal/ports/esp32c3/network/README.md).
The portable modules here contain no board-register or radio SDK dependencies.

## Contract and limits

`netring` uses caller-owned storage and an explicit count, so all configured
slots are usable, including a length-one queue. Back/front insertion, peek,
receive, discard and reset are bounded by that storage. Overwrite is accepted
only for a one-item queue. Invalid/full requests leave previous data intact.

`netwait` converts SDK ticks using the actual configured frequency, with one
absolute microsecond deadline and upward millisecond rounding. Every stale
notification or early native timeout recomputes the remaining budget; it cannot
restart the full timeout. Infinite waits remain distinct from signed finite
native waits. A reversed/overflowed clock fails explicitly. Poll/ISR paths do
not require a clock reading or perform 64-bit division.

`netpool` reserves eight queue slots over 8,192 payload bytes: two 512-byte,
five 1,024-byte and one 2,048-byte partition, tried in ascending size. Sixteen
semaphore slots and thirty-two mutex slots have separate reservations. External static queues may instead use caller-owned
storage of the declared size, while consuming one of the eight metadata slots.
Their control-buffer pointer is retained as the handle. After successful static
queue admission, `c3nq_static` sets its first byte to a nonzero initialization
marker: the pinned lwIP port checks `StaticSemaphore_t` against all-zero storage.
Operational queue/semaphore state remains in the fixed pool; no vendor structure
fields are interpreted. Failed or duplicate admission leaves caller bytes untouched.
Callers supply writable control storage of the SDK-declared size and retain it
for the lease lifetime. lwIP clears its static control storage after deletion.
The different resource classes have separate capacity limits; mutexes do not
waste queue payload storage. A queue too large for its selected storage or an
exhausted class returns failure without a heap fallback.

The original equal-size layout rejected the pinned Wi-Fi task's 200 by 8-byte
queue. The revised layout preserves the same total payload/metadata/native-object
reservation while admitting that 1,600-byte request. There is only one partition
above 1 KiB; this is a documented capacity limit. Oversize requests and exhausted
suitable partitions fail without consuming a smaller slot. Source review and the
linked `pp_create_task` disassembly establish the request shape; the controlled
radio comparison below confirms the corrected path on hardware. The frozen SPI dataset retains its
preceding equal-partition image and must not be relabelled to this revision.

Manual BARR-C review: queue indices are restricted to 0..7 before offset lookup.
The flat byte-array partitions cover [0,512), [512,1024), five consecutive 1 KiB
ranges, then [6144,8192), with no cross-subarray pointer arithmetic or overlap.
Ring initialization checks capacity/item multiplication before writing metadata.
Failed fits try at most eight slots under the existing guard. No heap, new native
object or C exception is introduced. Host tests fill/read all mixed-size queues,
check exact capacity, protect neighbouring payloads and exercise release/reuse;
the native fixture adds the actual 200 by 8-byte shape and exhaustion checks.

The queue adapter precreates 32 native notification semaphores and 32 inherited
mutexes at startup, before SDK/libc clients. Queue leases use two notifications,
semaphore leases one, and mutex leases one native mutex. Delete only releases a
logical lease. A pending operation or held mutex prevents release; failed native
unlock retains ownership. Callers must stop using handles and caller storage
after successful release. Native objects are never deleted or created by later
lease operations, reconnects or exhaustion paths. Startup failure is terminal
until reboot; partial resources are retained rather than retried.

Notifications are hints; protected queue/count state is authoritative. Copy and
state updates occur under saved/restored single-core interrupt exclusion. Waits
occur outside it. An operation pins its lease across a wait, preventing a
concurrent release/rebind. Mutex depth and logical ownership are protected, with
native unlock and ownership publication in the same outer critical section.
The native kernel supplies priority inheritance. A successful ISR operation sets
a conservative wake hint without clearing an earlier hint; the native ISR tail
makes the actual scheduling decision.

SDK crypto initialization also takes locks before native tasks exist. The owned
pool reserves `UINT32_MAX` as a serialized startup owner, distinct from every
positive native task ID. Startup mutex operations preserve normal/recursive
ownership but use neither a clock nor a native lock. Every lock must be released
before task execution begins: a retained startup lock rejects later task access
without acquiring its unrelated native mutex. The C3 port never returns to
startup ownership after observing a task, and ordinary ISR mutex use is rejected.
Startup ownership prevents pre-scheduler `pthread_mutex_unlock` assertions.
Scoped execution evidence is in the
[core bench record](../../qualification/esp32c3-network-core-20261027.json);
it does not establish combined radio acceptance.
The startup initializer and logical boot-lock helper are mapped to flash: they
run with cache enabled. Qualification checks their placement while keeping all
SDK queue/event entry points in internal RAM. This preserves the RAM-code limit
instead of increasing it after the first corrected Wi-Fi map exceeded it.
Manual review: the reserved owner cannot collide with signed positive native
IDs, recursion is checked before increment, and all state changes share the
existing guard. No heap, kernel object, ABI alias or C exception is added.

## Pinned adapter integration

`tools/target/qualify_kernel.py build --runtime-port --static-net-queues` selects
the owned adapter; `--configuration-app` may be added. The option does not enable
radios. Default application/fixture builds compile these sources but do not
select their early initialization or ABI aliases. The isolated UART fault fixture cannot
be combined with this mode.

The owned build removes the vendor `shim_queue.c` compilation input, redirects
24 queue/scheduler linker symbols and isolates the vendor scheduler-state
definition. It does not edit any vendor source or revision. Qualification checks
the effective C99/warnings-as-errors commands, exact alias addresses, IRAM
placement of owned entry points and absence of the vendor queue object.
The scheduler-state result matches the actual pinned SDK values (suspended 0,
not started 1, running 2), including early boot. The upstream shim header uses
different running/suspended constants; these cannot be copied into owned
interfaces. The current probe's SDK tick rate and shim header both use 1000 Hz,
matching the native 1 ms timer. No current tick-rate mismatch is established.

SDK-facing entry points, queue storage and lease operations reside in internal
RAM. The wait arithmetic and event-state core, together with native kernel
notification and mutex callees, are flash-resident. Operations that could reach these paths
while flash cache is disabled **return failure**; they do not claim cache-off
radio compatibility. This behavior must be exercised with radio traffic and
flash saves before acceptance. Native stacks for the tasks created by the isolated
radio diagnostics are bounded as described below. Notification, timer-callback,
BLE allocation and lwIP/service paths remain separate open work.
The vendor timer callback currently runs in task-independent context, whereas
[FreeRTOS specifies timer-service task context](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch06.md).

The [pinned source audit](../../qualification/esp32c3-wireless-source-audit-20261027.json)
records the exact files and task/notification/TLS/timer gaps at its historical
revision. The staged task-wrapper patch now acquires caller-owned stacks from
`c3task`, creates native objects without a stack heap path and defers cleanup to
a permanent reaper task. Generation-tagged handles and ordered retry states cover
creation rollback, self/external deletion, concurrent requests and stale handles.
The patch never edits the pinned vendor checkout. This is build/link and host-test
evidence, not proof of stack high-water, scheduling or cleanup behavior on hardware.
The patch implements the selected one-entry task-notification contract; board
behavior is not yet qualified. Next work includes a permanent timer service with
bounded commands and explicit NimBLE/lwIP/service storage. A blanket SDK heap exemption
would not satisfy the requirement.

### Fixed diagnostic task stacks

The Wi-Fi profile reserves four 16-byte-aligned slots: 2,816 bytes for the event
loop, 3,584 for TCP/IP, 4,096 for the ESP timer task and 6,656 for the Wi-Fi task
(17,152 bytes total). The BLE profile reserves three 4,096-byte slots for the
ESP timer, NimBLE host and controller tasks (12,288 bytes total). The profiles
are mutually exclusive build selections; there is no combined Wi-Fi/BLE claim.
The native kernel has a separate 4 KiB interrupt stack, so these task reservations
do not add the obsolete per-task interrupt margin.

Allocation uses a bounded best-fit search and records the owning generation.
Oversize or exhausted requests fail without heap fallback. A slot is released
only after task deletion, semaphore deletion and observer release succeed. The
permanent 1,024-byte reaper runs at native priority 31 (idle is 32), polls every
100 ms and attempts at most four cleanup records per pass. Its own stack is not
allocated through the pool. See the [Buddy 1 report](../../../reports/buddy1/README.md)
for failure recovery, compiled tests, final ELF budgets and remaining hardware work.

### Integration review: static-task alignment

The merged `c3task` uses a C99 byte array with 15 bytes of alignment padding for
the complete task pool, and a separately padded reaper stack. A bounded offset
selects an aligned pool base; each slot starts at a fixed sum of the preceding
capacities. Compile-time checks require those capacities to preserve alignment.
Every usable slot retains its original capacity and the arrays do not overlap.
`uintptr_t` calculates and checks alignment without reconstructing a pointer.
Startup fails closed if alignment or resource creation fails. The zero-slot
configuration reports an unsealed pool. The existing interrupt guard still
protects the reaper and pool state; no new task, heap allocation, interface,
compiler exception or warning suppression is introduced. The manual BARR-C
review checked types, pointer provenance and bounds, initialization, fixed
storage, task ownership, concurrent access, error paths, module interfaces,
formatting and applicable language rules. Target timing and stack use are not
established by this review.

A fresh station build using dummy network credentials and a generated test
certificate passed its existing fixed-memory budget on 2026-11-07. The image
uses 272,156 of the 278,528-byte total limit; `c3task.c.obj` uses 18,499 of its
18,504-byte reservation, leaving only five bytes in that module. This is a
link-time result, not measured runtime heap or stack headroom. Build evidence is
in `C:/Users/kkak4/esp/uthome-station-recheck-20261107-r2/evidence.json` and
the local budget record is `build/station-recheck-memory-r2.json`. A separate
Bluetooth diagnostic also built at
`C:/Users/kkak4/esp/uthome-ble-pool-recheck-20261107/`; its runtime behavior
and a new memory-budget result remain unverified. Neither image was flashed.
The earlier station check (`build/station-recheck-memory.json`) failed before
the padding change and is retained. Historical budgets below describe their
original source, not the revised integration.

An earlier unflashed Wi-Fi diagnostic built successfully on 2026-11-07 from the
pre-padding-change source (`C:/Users/kkak4/esp/uthome-cleanup-wifi-20261107/`). Its
corrected budget check is **not passing**: `c3task.c.obj` uses 18,544 bytes
against its 18,504-byte limit; module reservations total 168,998 bytes against
the 168,192-byte application limit; and RAM code uses 57,208 against 57,152
bytes. The checker now attributes the task archive to application storage.
The generated local record is `build/cleanup-wifi-memory-budget-corrected.json`.
No limit was raised. The revised source has not been rechecked against this
separate diagnostic budget; the passing station result above does not qualify it.

## Verification and manual BARR-C review

The host C tests use warnings as errors and actual compiled modules. They cover
25,000 seeded ring operations against a deque, one-slot/full/front/wrap/overwrite
behavior, buffer canaries, arithmetic limits, actual tick conversion, stale
wakeups, class exhaustion, static caller storage, blocked-release interleavings,
ISR polling, mutex ownership/recursion and failed-clock/reinitialization paths.
These host tests do not establish RF behavior, native scheduling or physical timing.
Fresh target build/board results must be recorded separately.

Manual review for BARR-C rules 1-8: owned code is C99 with paired module headers,
named callback signatures and exact external ABI widths. Enum conversions follow
explicit bounds; byte-offset multiplication is checked against actual storage.
No variable-length arrays, heap paths, unbounded copies, pointer-key dereferences
or new compatibility exceptions are introduced. Metadata lifetime is protected
across callbacks; waiting loops use an absolute deadline (or an explicit infinite
wait). Every error preserves valid ownership/storage or returns a documented
failure. The existing vendor priority-one hook reaches an owned C99 boundary through
a source-specific build substitution. A checked source-scoped rename calls the exact
pinned native table initializer before reserving resources; vendor files and
compiler profiles remain unchanged. The port's
interrupt mask and native handle conversions are exact 32-bit C3 values.
Function boundaries separate queue data, clocks, leases and target adaptation.
The complete formatter/lexical gate and real target build remain required;
this source review is not whole-system static-memory or hardware qualification.

The native fixture `nqcheck` exercises 128 lease/release cycles, exact one-item
capacity, front/back/peek/overwrite, static caller storage, counting semaphores,
recursive mutexes and real blocked sender/receiver handoff. It borrows one of the
four existing workers and compares allocation/object counter fields before and
after. No peripheral I/O, new task or new synchronization object is requested by
these tests. Retain fresh execution results with the exact fixture identity.

The selected queue/event/startup/task-stack variant uses
`tools/target/network-budget.json`: 278,528 bytes overall, split into 168,192
bytes of application data, 53,184 bytes of SDK/kernel data and 57,152 bytes of
RAM code. The older `netqueue-budget.json` belongs to the queue-only experiment.
Neither budget alone proves that radio services fit.

## Scan-only feasibility diagnostic

`--wifi-probe` requires `--runtime-port --static-net-queues` and rejects the
configuration application, isolated UART test and upstream examples. It retains
`c3app_run` and its complete fixed RAM/code footprint but does not run USB
configuration or peripheral workloads. Four permanent peripheral workers and
idle exist; the probe scans, stops, restarts, scans again, then stops the radio.
It does not associate, create an AP, start IP services, use credentials, initialize
NVS or erase flash. RF calibration persistence and Wi-Fi NVS are disabled. The
compiled SDK tick rate is 1,000 Hz, matching the native millisecond timer. No
current tick-rate mismatch is established; verify the effective build settings.

The probe records SDK stage results, AP counts (no SSID list), actual internal
free/largest heap blocks, application/startup/normal allocation counters and
kernel-object creation counters. Startup is sealed after the first scan so
restart activity is distinguishable. SDK timer/event/driver initialization can
still allocate and create objects through unadapted paths: this is explicitly a
**noncompliant diagnostic**, never a static-memory or combined-workload pass.
These observations determine the required fixed resources before selecting radio
service budgets. On error the diagnostic reports its last completed stage and
must be replaced with the retained application image.

Manual review: the report is caller-owned fixed storage; scan records remain
inside the SDK and are released without copying identities. API return codes
propagate, counts have exact SDK widths, and internal-RAM sizes fit 32-bit C3
`size_t`. No authentication or persistent configuration flow is introduced.
The owned adapter remains C99; its source-scoped external-header spelling
adaptation is verified by the build tool and does not modify vendor files.

`--ble-probe` selects an alternative nonconnectable advertising diagnostic without
Wi-Fi activation. The SDK's invisible `ESP_WIFI_ENABLED` symbol describes chip
capability and cannot be disabled through defaults; qualification instead rejects
linked Wi-Fi initialization/start/association entry points. It retains the same
inactive USB core and four permanent workers,
then initializes NimBLE, advertises `UTHOME-DIAG` for five seconds, stops and
repeats once. It records allocation/object counters and free/largest heap blocks
before initialization, after the first advertising interval, after restart and
after stop. NVS persistence is disabled; no GATT connection, pairing or phone
interaction is requested. SDK host/controller allocations remain unqualified.
The host task is retained until reset.

The BLE probe selects the pinned SDK's flash-only controller to reduce RAM code.
It removes the incompatible legacy ROM dispatch patch from compilation and link
inputs without changing vendor sources; the qualifier rejects remaining patch
symbols. Flash operations while BLE is active remain unqualified because this
option has performance/cache limitations. The probe performs no flash writes.

NimBLE is compiled in the SDK-only `uthome_radio` component because its BSD
`sys/queue.h` conflicts with the native kernel header of the same name. The
owned `c3bport` interface supplies typed critical-section, delay, audit and task
exit wrappers across that boundary; qualification rejects native include leakage.
An explicit link anchor retains this bridge despite static-archive ordering.
Manual review: the eleven-character advertising name occupies twelve bytes
including its terminator; callbacks publish fixed state under restored interrupt
exclusion, synchronization waits are bounded, and task exit does not delete its
native object. The probe adds no C deviation or application allocation exemption.

The scan profile disables Wi-Fi IRAM/RX-IRAM speed optimizations, trading peak
throughput for internal RAM. Measure each linked image against the unchanged
budget; do not increase a limit to turn a failed experiment into a pass.

## Fixed events and bounded startup pool

The experimental selection now also removes only `shim_evtgrp.c` and the weak
`mtk_startup.c` provider from build inputs. Eleven checked SDK event aliases
reach `c3event`; the pinned kernel's ordinary initializer and memory-boundary
variables are retained through exact owned aliases. Vendor sources are unchanged.
Default builds keep the upstream startup/event selection.

`netevent` reserves eight groups and sixteen wait records. Each group has 24
usable bits; reserved bits, unknown keys and exhausted slots fail without heap
fallback. Static APIs preserve caller-provided identities without dereferencing
them. All waiters satisfied by a set receive the same snapshot before the union
of their clear masks is applied. Any/all waits, unrelated bits and atomic sync
barriers retain their separate semantics. Deletion wakes pending waits with
zero, while already completed waits keep their result; a retired group cannot
be rebound until every waiter finishes. Stale notification counts cannot satisfy
a different wait. Tasks must not reuse a completed token or a deleted handle.

`c3event` reserves sixteen additional native binary semaphores at startup,
bringing this selection to 48 notifications and 32 inherited mutexes. Waits use
one absolute deadline and reject invalid blocking contexts. ISR set/clear paths
perform bounded direct work over at most sixteen waiters; native dispatch occurs
at the ISR tail, without a timer-daemon request queue. Cache-off operations
currently reject. These are explicit port limits, not a claim of unrestricted
SDK/radio compatibility. Kernel notification errors stop new event operations;
there is no dynamic recovery allocation.

`c3ewait` isolates the pinned native calls without retaining state. Event policy,
the sixteen handles and their boot lifetime remain in `c3event`. Host tests run
the real adapter with controlled native-boundary results, including partial
creation, full/broken hints, stale/early wakeups, failed/reversed clocks, wait
errors, deletion and registration reuse. They do not emulate native scheduling.
The cache-availability function is explicitly mapped to IRAM because it executes
before cache-off rejection; target qualification rejects a missing/flash mapping.

Manual BARR-C review of this boundary: native IDs remain target-width signed
integers, finite timeout conversions are range-checked, and infinite waits keep
their separate native spelling. Saved interrupt masks restore exactly once on
each path; hints publish inside exclusion and waits occur outside it. No heap,
new permanent field, object, exception or vendor change is introduced. The
flattened pool lease loop preserves collision termination, bounded partition
search and protected publication; its early duplicate-key exit restores the
same saved mask. Source moves preserve module bytes and historical identities.

`c3boot` supplies a 12,288-byte, eight-byte-aligned native startup pool. This is
separate from the four 8 KiB worker stacks. Scoped USB-core evidence covers this
variant; changes still require boot, stack-margin and allocation verification.
It is not a general-purpose application heap or permission for later native
allocation. Failure to initialize is terminal until reset. The experimental
`tools/target/network-budget.json` redistributes the same 278,528-byte total:
168,192 application data, 53,184 SDK/kernel data and 57,152 RAM code. It is a
reservation to test, not evidence that the complete wireless system fits. The
isolated Wi-Fi link leaves only 126 bytes beneath that total; any growth requires
deliberate remeasurement and rebudgeting, not a relaxed cap.

Manual BARR-C review (rules 1-8): these modules remain C99 with paired headers,
fixed-width SDK/native ABIs and no added exceptions. Stack/pool alignment uses
standard `uint64_t` storage; exact linker aliases avoid vendor naming in owned
definitions. Pointer identities are compared only, never dereferenced or ordered.
Masks use unsigned shifts bounded to sixteen records; array indices originate
from bounded searches. Metadata and notification publication share interrupt
exclusion; blocking occurs after restoration. Every registration releases once,
including timeout and delete. No native object is created after initialization.
Host tests cover capacity, key collisions, broadcast clearing, barriers, deletion,
timeout and 6,000 independently modelled interleavings. The board fixture adds
128 event reuse cycles and a real blocked two-task barrier, with unchanged audit
counters required. Build/link proof and physical execution are separate gates;
the corrected USB study uses this variant after the linked SPI/core bench qualification. Radio qualification remains separate.

## Radio evidence and diagnostic counters

[Retained radio results](../../qualification/README.md) cover isolated Wi-Fi
scanning and BLE advertising/read-only GATT. They do not qualify association,
authentication, services, coexistence or static compliance. SDK heap activity
remains an unresolved requirement. Earlier zero-byte captures are inconclusive.
Observed allocation counts/volume are cumulative, not peak live RAM.

`c3nq_diagnostic` records dynamic-API call/failure counts and
the last rejected request's length, item size and kind, only in the Wi-Fi probe.
It stores 20 bytes, uses saved/restored interrupt exclusion and saturating counts,
and performs no allocation/logging in queue operations. A task-context snapshot
prints the evidence afterward. Static-API requests are outside these counters.
Inspect each image's RAM-code margin and the
[task/timer/notification audit](../../qualification/esp32c3-wireless-source-audit-20261027.json)
before service/coexistence qualification. A passing aggregate map is not enough.

Manual review for this diagnostic: fixed-width values preserve the SDK request,
counts cannot wrap, null output is rejected, and the snapshot never changes
queue state. Ordinary application builds compile out the diagnostic state/code.
Neither the diagnostic nor its observation authorizes increasing a buffer budget,
allowing heap fallbacks or treating radio initialization as application completion.

## Read-only GATT diagnostic

`--ble-gatt-probe` requires the isolated `--ble-probe` selection and fixed network
queues; it is incompatible with the configuration application, Wi-Fi and model
diagnostics. It enables one peripheral connection and advertises `UTHOME-GATT`
for a bounded 45-second window. Service
`ec441e20-8a4b-4aee-9db3-75b77d880001` exposes read-only characteristic
`ec441e20-8a4b-4aee-9db3-75b77d880002`: twelve bytes containing `UTHG`, little-endian
version 1 and live uptime modulo 2^32 milliseconds. There are no device readings,
profile mutations, pairing requests or NVS persistence. This diagnostic is
unauthenticated; it is not the planned authenticated configuration service.

Manual BARR-C review (rules 1-8): `c3gatt` owns constant SDK definitions, one
saturating four-byte counter and an explicitly encoded bounded stack response.
The SDK callback types match their headers without casts. Byte shifts are unsigned
and bounded; the signed timer is checked before conversion. Append failures return
an ATT error; the read count advances only after successful append. Connection
state and callback counts use saved/restored interrupt exclusion; SDK calls and
delays occur outside it. Disconnect restarts advertising only before the bounded
window closes. The controller checks SDK return values, resets, host exit and
minimum observed connections/reads. The SDK may allocate inside initialization
and connection paths; audit counters expose this rather than exempting it.
The exact ordinary/diagnostic compiler and link inventories remain separate.

`--ble-static-host` is a separate, opt-in build experiment requiring the isolated
BLE diagnostic. It sets `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=n`, selecting the
pinned SDK's static host-global definitions. The qualifier checks the effective
setting and keeps `whole_system_static_qualified` false. This does not disable
controller allocations, NPL pool backing allocations, task stacks, timer resources
or all packet/service storage. A name containing "static" is insufficient proof.
Measure its linked RAM against the unchanged budgets and retain separate actual
heap observations before choosing it for the integrated application.

Windows BLE tests need device access. Two client contexts may reuse one physical
connection; require board-observed disconnect/reconnect and allow the client's
eight-second settling interval. Check changing uptime and rejection of writes.
Unvisited observation slots are unavailable, not evidence of zero heap use.
See the [retained GATT bench](../../qualification/esp32c3-gatt-settle-20261027.json).

Full static compliance, sensor/configuration services, authentication, coexistence
and phone acceptance remain open. Query the actual board identity before testing;
a historical restoration record does not establish what is installed now.

## Proposed production service contract (review draft)

Status: proposed, not approved, implemented or hardware-qualified. This section
specifies the independent service work and the integration decisions still
needed. It does not change the experimental budget or enable production radios.
Machine-readable proposals are in `tools/target/network-service-contract.json`.

### Ownership and prerequisites

The service implementer owns c3wifi/c3ble/c3gatt and new transport services.
Member 1 owns shared task, notification, timer and resource-pool compatibility.
The existing application owner alone calls cfgctrl and owns parser hooks,
activation, rollback and persistence. No callback may create another Apply/save
path. USB remains setup/recovery. SDK compatibility changes require shared review.

Before integration, inventory every radio/lwIP/BLE/TLS task, stack, queue,
notification, timer, packet pool and native object. Record startup allocation
rules and the sealing boundary. Reconnect/exhaustion must not create objects or
use forbidden allocation paths after sealing. Fixed pools require an explicit
approved allocation policy; available heap is not an approved reservation.

### Proposed bounds

- Wi-Fi station only, one configured AP; HTTP two clients, MQTT one connection,
  BLE one peripheral connection. TCP socket demand includes the listener, DNS,
  TLS handshakes and closing/TIME_WAIT state; two clients is not a socket budget.
- One mutation across USB/HTTP, no queued mutations. One exclusive 8192-byte
  upload slot; release only after the controller no longer borrows its contents.
- HTTP headers including request line <=1024 bytes, profile body 1..8192 bytes,
  shared response staging 2048 bytes. One request per connection then close.
- MQTT topic <=128 UTF-8 bytes, payload <=1024 bytes, complete encoded packet
  <=1280 bytes. One replaceable pending payload and one in-flight operation;
  their separate reservations must be counted. No offline history.
- BLE application frame <=128 bytes, one active frame and one replaceable pending
  frame per connection. ATT fragmentation overhead is additional.
- At most one telemetry publication per second per transport. MQTT rotates sources
  fairly; this is a transport rate, not a promise of one reading/source/second.
- Proposed application service stack 8192 bytes. This is additional to SDK stacks
  and the permanently reserved 32 KiB peripheral storage. Stack count, priorities,
  periods, CPU budget and high-water margin remain unapproved. No peripheral slot
  is borrowed. Do not sum proposed buffers and claim a complete memory budget.

The existing 278528-byte experimental DRAM ceiling remains unchanged. Application,
SDK/kernel, RAM code and RTC categories retain existing meanings. Obtain a full
link map and allocation/object measurements before changing any ceiling.

### Wi-Fi and broker state

Wi-Fi states: DISABLED, CONNECTING, WAITING_FOR_IP, ONLINE, BACKOFF.
One attempt at a time; proposed whole association/IP deadline 30 seconds.
Retry delays are 1,2,4,8,16,30 seconds with capped bounded jitter; reset after
60 seconds stable online. Authentication/IP/AP-loss errors remain distinct.
Network loss must not stop local sensors, display, rules or USB. Do not save
credentials or write flash on reconnect. Credential provisioning, schema placement,
secure storage and validation need review through the existing controller path.

MQTT states: DISABLED, WAITING_FOR_IP, CONNECTING, ONLINE, BACKOFF.
Apply independent capped broker backoff; Wi-Fi online does not mean broker online.
Keepalive proposal 30 seconds; connect/handshake/write deadlines must be finite
and platform-approved. Rebuild telemetry from current cache after reconnect;
never replay an unbounded queue. Discard encoded frames after a generation change.

### Shared observation schema

Capture copied samples through shared_capture; never drain display/rule queues.
All wire formats explicitly encode fields; never send native structure bytes.
Version 1 includes node_id, boot_id, active_revision, runtime_generation,
device_id, sequence, timestamp_ms, age_ms, quality, status and typed values.
Use the existing runtime clock and cache freshness/status semantics. timestamp_ms
is measurement completion uptime, not UTC or network arrival time. Missing/fault
values are null; stale successful values retain stale quality. Boot identity
must distinguish rebooted sequence spaces without requiring a flash write.
Node identity/boot identity encoding and generation source need implementation review.
Units are temperature_c, pressure_pa, distance_mm, echo_us, target x_mm/y_mm,
speed_cm_s and resolution_mm. Targets retain at most three explicit slots.
Health uses existing event codes/counts. No secrets appear in observations.

### MQTT v1

Prefix: uthome/v1/{node_id}/. Restrict node/device topic segments to reviewed
ASCII letters, digits, underscore and hyphen; no slash, wildcard or NUL.

| Topic suffix | QoS / retained | Content |
|---|---|---|
| availability | 1 / yes | version, boot_id, online Boolean; offline Last Will |
| status | 0 / no | boot, revisions, runtime/network states and counters |
| telemetry/{device_id} | 0 / no | One source using the shared schema |

Last Will is registered before connect completion. Offline state is best effort
and may be delayed by broker detection. Availability is connectivity, not sensor
health. No subscriptions or configuration commands in v1. Reject oversized
encodings without truncation; increment a bounded/saturating diagnostic counter.
Broker authentication and certificate-verified TLS are production requirements;
trust/time bootstrap and TLS storage must be resolved, not bypassed.

### HTTP v1

Production uses HTTPS and a per-device bearer credential. Provision through the
trusted USB setup path. Credential generation, rotation, certificate provisioning,
constant-time secret comparison and protected storage are integration work.
Plain HTTP bearer tokens do not satisfy this production design. All endpoints
initially require authentication. No wildcard CORS, browser credentials, secret
logging or unauthenticated configuration routes. Reject unsupported methods.

| Endpoint | Contract |
|---|---|
| GET /v1/identity | API version, node identity, firmware identity |
| GET /v1/capabilities | Compiled capabilities and separate qualification state |
| GET /v1/status | Boot, active/saved revisions, runtime/network and pool counters |
| GET /v1/telemetry | At most four source rows per page; bounded encoded response |
| POST /v1/configuration | Complete existing node JSON; submit to cfgctrl owner |
| GET /v1/configuration/result | Latest retained request result only |

Telemetry query uses offset 0..11 and limit 1..4. Replies include capture ID,
generation and next offset. Pages are separate captures; clients restart if boot,
revision or generation changes. No claim of an atomic multipage snapshot. An
encoding exceeding staging capacity returns a defined error, not truncated JSON.

POST requires Content-Type application/json, one valid Content-Length,
Authorization, X-Request-Token (1..64 reviewed ASCII characters),
X-Expected-Boot-ID and X-Expected-Revision (canonical uint32 decimal).
Reject chunked/multipart/compressed bodies, conflicting/duplicate length framing,
trailing bytes and unsupported transfer encodings. Authenticate and reserve the
upload slot before accepting a body; close safely on early rejection. Whole
request receive deadline proposal is 10 seconds; never restart per byte.
Header count/line bounds and partial-response deadlines also need fixed limits.

The owner rechecks boot/revision immediately before execution. Preserve cfgctrl's
full lexical, semantic, resource, activation and persistence validation. Network
validation may reject framing earlier but cannot replace target validation.
The current strictly increasing per-boot controller IDs require one shared USB/
HTTP arbitration design: HTTP must not create an independent sequence or silently
rewrite existing USB IDs. The implemented global-ID arbitration resolving this proposal is specified below.

Retain the latest token/content/result only. Same token with exact same retained
body/preconditions returns the retained result; conflicting content returns 409.
Evicted/unknown results are unavailable, never proof of failure or permission to
retry automatically. Do not use CRC alone as exact-content identity. A disconnect
after admission does not cancel controller work. 202 means accepted, never saved.
Result fields expose received/validated/activated/saved flags, controller status,
active/saved revisions, durable-known and recovery. Save failure after activation
must report active but unsaved, preserving controller truth.

| HTTP code | Meaning |
|---|---|
| 200 / 202 | Read/completed result / accepted pending result |
| 400 / 401 | Malformed request / invalid authentication |
| 404 / 405 | Unknown endpoint or unavailable retained result / unsupported method |
| 409 | Boot, revision or token conflict |
| 413 / 415 / 422 / 431 | Body too large / unsupported representation / rejected profile / headers too large |
| 503 | Busy, exhausted capacity or temporarily unavailable snapshot |

Error bodies use {version:1,error:stable_code}; fixed messages omit secrets.
Controller/application failure results remain explicit even if HTTP delivery succeeds.
TLS handshake exhaustion and physical socket exhaustion may close/reject before
an HTTP response can be sent. Bound authentication attempts without lockout that
permanently blocks recovery; retry/rate policy remains to be agreed.

### BLE v1

Proposed UUID base ec441e20-8a4b-4aee-9db3-75b77d88xxxx:
service 0010, identity 0011 (read), status 0012 (read/notify), telemetry 0013
(notify). Existing 0001/0002 uptime definitions remain diagnostic only.
No configuration, actuator or provisioning writes. CCCD subscription writes are
supported solely to enable/disable notifications; unsupported writes are rejected.

Identity/status/telemetry use explicit versioned little-endian binary encodings.
Exact field offsets and type codes must be frozen before publishing a client;
UUIDs alone are not a complete GATT wire specification. Proposal: fragment header
is version u8, frame_kind u8, frame_sequence u16, fragment_index u8,
fragment_count u8, total_frame_bytes u16 (eight bytes). Fragment to negotiated
ATT_MTU-3 including this header; default MTU must work. Limit total frame to 128
bytes, reject invalid count/index/length, and expire incomplete reassembly after
2 seconds. Read responses require the same bounded long-read/offset policy.
Telemetry rotation includes all sources; disconnect discards partial frames.
Notifications are best effort; no application retransmission backlog.
Pairing/encryption, privacy and bonding storage remain explicit production
security decisions. Read-only does not imply confidential sensor information.

### Implementation and acceptance gates

1. Portable bounded state machines, encoders and request ownership tests.
2. Fixed shared compatibility/resource agreement and real target build.
3. Wi-Fi association/reconnect and read-only real sensor service.
4. MQTT/BLE telemetry with TLS/security and resource gates satisfied.
5. Authenticated HTTP mutations through the existing owner, USB arbitration tested.
6. Combined radio/peripheral/USB/flash acceptance and fresh evidence.

Required tests: oversized/malformed/slow input; invalid credentials; exhausted
clients/pools; repeated reconnect; stale revisions; duplicate and concurrent USB/
HTTP mutations; disconnected clients after admission; broker outage; BLE minimum
MTU and slow subscriber; plan changes during publication; save failure; radio
traffic during flash saves; allocation/object counters; stack headroom; sensor/
display timing and rollback. Cache-off failures must recover without deadlock or
unsafe calls; separate tasks alone do not prove safety. Hardware timing thresholds
must be approved before measurement. Leave unperformed checks explicitly open.

Client handover must contain exact build identity, setup/provisioning steps,
HTTP requests, MQTT subscription examples, BLE decoder and reproducible failure
injection. No production or combined-static claim follows from this draft.

## Portable service implementation: MQTT staging and reconnect

`netmqtt.c/.h` now implement actual MQTT 3.1.1 QoS-zero PUBLISH packet
encoding and one replaceable pending telemetry slot. Topics are explicit-length
printable ASCII without wildcards; payloads are binary. Size bounds are 128-byte
topic, 1024-byte payload and 1280-byte packet storage. Invalid offers preserve
previous pending data. Taking a packet copies it to separate caller-owned
transmit storage: newer pending readings cannot corrupt a partial socket write.
A generation mismatch discards pending data. The caller must also cancel any
already copied active packet on reconnect/plan change. Replacement counts saturate.

`netretry.c/.h` implement caller-driven readiness/attempt/online/backoff state.
Use independent contexts for Wi-Fi and MQTT; MQTT upstream is IP readiness.
An admitted attempt returns 1 once, then the platform starts bounded asynchronous
I/O. Report success only after actual IP/broker readiness. Attempts expire after
30 seconds; failure delays grow 1,2,4,8,16,30 seconds with caller-provided 0..250 ms
jitter and a 30-second total cap. Sixty seconds online resets the delay on failure.
Upstream loss cancels readiness without blocking. Clock regression, invalid jitter
and deadline overflow return failure without changing state. The adapter must
cancel/join old attempts and reject stale asynchronous completion events before
reporting results; this portable API does not identify SDK event generations.

Both contexts require one serialized owner, initialized storage and the same
monotonic clock. They allocate no heap, create no task/object, call no SDK and
perform no I/O themselves. The optional c3app service hooks compose them with
netbroker/c3mqtt for CONNECT/CONNACK, TLS, keepalive, QoS-zero retained
availability and socket lifecycle, as documented below. QoS-one availability
remains unimplemented. Controller mutation arbitration is implemented below. HTTP/authentication
and GATT sensor encoding are separate modules. SDK/hardware qualification and
approval of the unchanged experimental resource limits remain open.

Reproduce portable checks from the repository root:

```console
python -B -m unittest discover -s tests/target -p "test_network_*.py" -v
python tools/c_standard/check.py --format PATH_TO_CLANG_FORMAT_20_1_8
```

This implementation run passed 36 host network tests, including nine new cases.
The tests compile actual C99 modules with -Wall -Wextra -Werror -pedantic.
Coverage includes exact MQTT bytes, maximum packet bounds/canaries, invalid topic
and payload rejection without state loss, 10000 pending replacements, active-copy
ownership, generation changes, capacity exhaustion, counter saturation, attempt
expiry/backoff cap, upstream loss, stable reset, clock regression and overflow.
This is host evidence, not radio, target-build or flash-save qualification.

Manual review against C_CODING_STANDARD rules 1..8: paired C99 modules and named
public interfaces; private schedule helper; initialized fixed-width state; explicit
unsigned packet encoding and bounded loops; topic/value limits bound complete
packets to 1157 bytes; byte casts are limited to 0..255; no raw struct serialization,
heap, hardware qualifiers, callback ABI casts or new deviations. Input/output must
not alias MQTT context. The caller owns serialization and lifecycle. No resource is
released by these functions; arithmetic errors preserve retry state. Existing
platform/task/IRAM contracts are unchanged. Real-target build and combined hardware
acceptance remain open because portable tests alone do not qualify the SDK bindings below.


## Implemented portable HTTP and BLE sensor services

This section defines the implemented subset; the production review draft above
remains a future proposal. The optional ESP binding and its build mode are described below.
`nethttp`, `netread` and `netble` are owned C99 services, compiled and exercised
against the actual sample-cache implementation in host tests. Existing SDK compatibility implementations, active budgets and diagnostic GATT
remain unchanged; application hooks select the separate optional services.

### HTTP admission and reads

`nethttp_init` initializes one connection with a 16..64-byte visible-ASCII secret.
Use a securely generated per-device credential, provisioned through a reviewed
setup path; token syntax checks do not establish entropy. Call `nethttp_feed` for
received chunks and with zero bytes during idle polls. It returns 0 incomplete,
200 authenticated/admitted, or a terminal HTTP error. Deadline is absolute 10 s
from initialization, including idle clients. Clock regression terminates with 408.
Terminal contexts are never reused. Limit accepted sockets in the future adapter;
this parser does not itself implement a listening socket or connection pool.

HTTP/1.1 and a nonempty Host are required. Header capacity is exactly 1024 bytes,
line length <=256, fields <=16, URI <=96. Reject bare CR/LF, controls, non-ASCII,
folded/invalid field names, duplicate Host/Authorization/Content-Length, nonzero or
noncanonical Content-Length and every Transfer-Encoding. Authentication compares
all 64 credential positions without an early matching-prefix exit; this is not a
hardware timing side-channel certification. Secret context storage must be erased
by its lifecycle owner after all users stop. Replies contain no credentials.

Implemented endpoints, all requiring `Authorization: Bearer TOKEN`:

| Endpoint | Body / behavior |
|---|---|
| GET /v1/identity | JSON version, node_id, 16-hex-digit boot_id, configuration_supported reflects controller binding |
| GET /v1/status | JSON version, boot_id, active_revision, saved_revision, runtime_generation, runtime_state, durable_known, wifi_online |
| GET /v1/telemetry?source=N | N is exactly one digit 0..7; canonical 112-byte binary sensor frame below |
| GET /v1/configuration | Controller boot/revision/request state; no profile or credentials |
| POST /v1/configuration | Authenticated bounded full-profile Apply through cfgmux/cfgctrl; specification below |
| GET /v1/configuration/result | Latest retained HTTP completion, matched by request ID and token |

Other routes, including capabilities and pagination from the
proposal, currently return 404; other methods return 405. Invalid credentials
return 401 with WWW-Authenticate: Bearer; oversize aggregate headers 431; malformed
framing 400; receive expiry 408. Capturing an absent source returns 404; busy/error
or inconsistent source metadata returns 503. All replies close the connection,
disable caching and declare exact Content-Length. Error JSON has version 1 and
error `request_unavailable`; HTTP status carries the error class. Only the configuration POST accepts a body, under the contract below.

`netread_reply` receives owner-copied controller/runtime metadata and a named
capture callback. Production binding should delegate capture to `shared_source`;
metadata and capture must be serialized with configuration activation. It does
not infer live metadata from globals or call cfgctrl. Missing/fault samples still
return a valid binary frame; 200 does not assert successful sensor measurements.
It copies output into caller-owned transmit storage and never holds a shared
guard across encoding or partial socket writes. Output >=512 bytes is the
provisional complete-response reservation; short buffers fail without partial
reply writes. Sensor source indices include disabled plan entries; correlate
boot/revision/generation and the copied device name, never assume fixed identity.

Identity/status use application/json. Implemented telemetry deliberately uses
application/octet-stream, one source per request, sharing BLE's exact canonical
frame. This is a smaller implemented subset than the proposed paginated JSON API;
do not advertise the proposal as supported. Only device sources 0..7 are routed
through HTTP currently; global health slots are not exposed by this endpoint.

TLS termination, credential provisioning, rate limiting, TCP lifecycle, certificate
trust/time bootstrap and shared controller arbitration remain integration gates.
The parser's credential check does not make plaintext HTTP secure. Production
must bind it behind authenticated TLS; it is not an onboard HTTPS server yet.

### Canonical sensor frame v1 (112 bytes)

All multi-byte integers are little endian. Environment floats use IEEE binary32.
The encoder copies normalized cache entries, never sensor queue items or struct
padding. Unused and reserved bytes are zero. No native pointers cross the wire.

| Offset | Encoding / meaning |
|---|---|
| 0..3 | u8 version=1, sample type=1..5, quality=0..3, sample status=0..4 |
| 4..5 | u16 source_index; 65535 reserved for global health |
| 6..7 | Reserved zero |
| 8..11 | u32 source sequence |
| 12..15 | u32 active revision |
| 16..19 | u32 runtime generation |
| 20..27 | u64 boot identifier supplied by owner |
| 28..35 | u64 measurement completion uptime ms |
| 36..39 | u32 age ms, saturated; UINT32_MAX when unavailable |
| 40..43 | u32 stale threshold ms |
| 44..75 | Source name, NUL terminated and zero padded, <=31 bytes |
| 76..111 | Explicit typed values, unused bytes zero |

Values: environment type 1 is pressure_pa float32 at 76 and temperature_c float32
at 80. Distance type 3 is distance_mm u32 at 76 and echo_us u32 at 80. Health
type 4 is existing event u32 at 76 and occurrences u32 at 80. Light type 5
is millilux u32 at 76 (0..200,000,000); remaining value bytes are zero. Targets type 2 uses
three 12-byte slots: word0 presence bit0 and resolution_mm bits16..31; word1
signed x_mm int16 and y_mm int16; word2 signed speed_cm_s in low16, high16 zero.
Absent slots are entirely zero. Missing samples have zero sequence/time/status,
unavailable age and zero values. Fault samples have zero values, distinct quality
and status. Stale successful values remain visible. Non-finite environment or
unknown health events become INVALID/fault with zero values. Times are runtime
uptime, not UTC; sequence and runtime generation are not a boot identity.

### BLE notification state and fragmentation

`netble_encode` encodes an owner-copied entry and metadata. `netble_offer` retains
one replaceable pending frame in a per-connection zero-initialized `netble_t`.
`netble_fragment` promotes a pending frame to immutable active storage. Repeated
calls return the same fragment until `netble_sent` confirms successful SDK enqueue.
SDK busy/error means retry later without advancing; disconnect calls netble_reset.
New offers never overwrite active fragments. A plan-generation mismatch discards
old active/pending frames. Reset on boot change too. Single-owner calls or explicit
serialization are required; these functions do not make concurrent callbacks safe.

At ATT MTU 23..517, fragment body <=MTU-11; total notification <=MTU-3. Header:
version u8=1, kind u8=1 (sensor), frame_sequence u16, index u8, count u8,
total_frame_bytes u16=112. Indices start at zero; count=ceil(112/(MTU-11)); sequence
increments modulo 65536 per promoted frame. MTU is frozen during active delivery.
At default MTU 23, ten fragments suffice. A successful SDK enqueue is not proof
of remote delivery: clients must reject missing/out-of-order fragments and expire
incomplete reassembly after two seconds. No application ACK/retransmission queue.
The future pump must enforce the proposed rate and capture source rotation fairly.

There is no production UUID registration, CCCD handling or NimBLE notification
call in these modules yet. Existing c3gatt uptime diagnostics remain unchanged.
Pairing/encryption and BLE storage still need review. Do not describe a passing
encoder test as a BLE sensor reading observed on a phone.

### Client and reproducible verification

`tools/target/network_client.py` provides a strict independent frame decoder,
bounded BLE reassembler and read-only HTTPS client. It requires certificate and
hostname verification, refuses redirects to protect Authorization, bounds response
reads and never mutates configuration. Set UTHOME_TOKEN without recording it in
source/history; obtain the reviewed device CA before live use. For an integrated
future server:

```console
python tools/target/network_client.py https://esp.local status --ca device-ca.pem
python tools/target/network_client.py https://esp.local telemetry --source 2 --ca device-ca.pem
```

Use BleReassembler(mtu).feed(notification_bytes) from a notification callback;
reset on disconnect. This is a decoder, not a discovery/pairing BLE client.

Run host verification:

```console
python -B -m unittest discover -s tests/target -p "test_network_*.py" -v
python tools/c_standard/check.py --format PATH_TO_CLANG_FORMAT_20_1_8
```

Tests compile actual C99 modules with warnings as errors. Added cases exercise
authentication, every header split boundary, malformed framing, request limits,
independent connections, deadline expiry, seeded malformed inputs, exact sensor
values/units, signed radar, stale/missing/fault semantics, real cache observation,
default/large MTUs, retry without advancing, pending replacement, generation
changes, short output buffers, strict client decoding and bounded reassembly.
These are host software checks; TLS sockets, RF, target link-map fit, connection
exhaustion on actual SDK, concurrent flash saves and timing remain unqualified.

Manual BARR-C rules 1..8 review: matching headers, public module prefixes,
private helpers, C99, fixed-size contexts, explicit lengths/unsigned wire shifts,
finite loops and null/capacity checks. Byte casts have bounded ranges; float bits
use memcpy after binary32 checks; signed targets convert modulo uint16. snprintf
uses fixed format strings with checked return/capacity; JSON node names restrict
delimiters. Parser/context ownership, nonaliasing buffers, valid native cache
objects and serialized metadata capture are caller obligations. No SDK ABI,
ISR contract, heap path, task object, allocation exemption or deviation is added.
The new modules are not in the target build inventory yet; target build and
combined acceptance are required when integration changes that inventory.


## Opt-in ESP transport bindings

The previously portable read path now has owned ESP adapters and an application
owner hook. This supersedes the earlier statement that no socket/GATT binding
exists. It is **experimental and off by default**. The BLE-only configuration
application now passes a real pinned ESP32-C3 build, compiler-profile, source/audit
and partition checks. The earlier host-only record retains its original missing-
toolchain result; fresh build evidence is linked below. Wi-Fi/HTTPS and combined
target verification, RF operation, TLS cryptography, flash-save/coexistence and
approved module reservations remain open. Nothing has been flashed.

### Code and lifecycle

- `c3wsta` creates the default RAM-only station interface and handles SDK Wi-Fi/IP
  events. It admits a single asynchronous association with netretry backoff and
  cancels an expired association before another attempt. WPA2 personal or stronger
  authentication is required by the station threshold; the present defaults disable
  WPA3, so enterprise/open/WPA3-only networks are unsupported. No NVS is initialized
  or written. An absent cancellation acknowledgment leaves networking waiting,
  rather than overlapping attempts. It does not stop any local worker.
- `c3https` binds an IPv4 listening socket (default private setup port 8443), sets
  listener and accepted sockets nonblocking and uses the pinned public
  [ESP-TLS asynchronous server APIs](https://raw.githubusercontent.com/espressif/esp-idf/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp-tls/esp_tls.h).
  There are two client slots, one 512-byte response buffer and one 128-byte read
  buffer. Each owner iteration accepts at most one socket and performs at most
  one TLS handshake/read/write operation per client. A full server closes an
  extra accepted socket before allocating TLS. Partial replies retain their exact
  bytes. Handshake, receive and reply phases each have an absolute 10-second
  deadline; wait results retry on later iterations. AP loss closes sessions and
  listener, then listening resumes on IP recovery. Listener failures retry no
  faster than once per second. TLS session cleanup and socket close are separate
  under the pinned API. No plaintext fallback or blocking handshake loop exists.
- `c3gsvc` starts the existing SDK NimBLE host, registers the service below, then
  advertises `UTHOME-SENSOR`. The SDK is configured for one peripheral connection.
  SDK callbacks exchange only short protected connection/subscription/status state;
  the application owner calls shared_source and feeds netble notification staging.
  It attempts one fragment per step. Busy sends retain the fragment, successful
  enqueue advances it. Disconnect/subscription/MTU changes reset delivery state;
  disconnect restarts advertising. One source slot is visited per second, rotating
  across indices 0..7, including absent slots. No configuration write exists.
- `c3net` creates a MAC-derived node identity and a random per-boot observation ID,
  copies controller/runtime metadata in the application owner, and binds capture
  to shared_source. Startup runs before audit sealing; pumping runs after the USB
  controller step. Startup/service errors do not return from c3app or invoke apply,
  save, restore, task cleanup or sensor/display stop. Boot IDs are observation
  disambiguation, not authentication secrets or persistent unique identifiers.

No edits were made to c3nq/c3event/c3boot, netpool/netevent, tkport, or the pinned
kernel/SDK compatibility sources. Build integration reuses the existing scoped
BLE flash-controller patch exclusion in the new opt-in mode. Ordinary/diagnostic
feature definitions remain disabled by default. Experimental Wi-Fi/HTTPS/BLE uses
unadapted SDK allocation/task paths: counters stay enabled, no allocation exemption
or production static-compliance claim is added. SDK calls may perform expensive
crypto or block inside unresolved compatibility paths despite nonblocking socket
I/O. Local timing and startup behavior must be measured on the real target.

### Exact GATT bindings

UUID base: `ec441e20-8a4b-4aee-9db3-75b77d88xxxx`.

| Suffix | Operation / bytes |
|---|---|
| 0010 | Primary sensor service |
| 0011 | Read-only identity, 44 bytes: version u8=1 at 0, reserved zero 1..3, boot_id u64 LE at 4, NUL/zero-padded node_id[32] at 12 |
| 0012 | Read-only status, 20 bytes: version u8=1, runtime_state u8, wifi_online u8, durable_known u8; active_revision u32 at 4, runtime_generation u32 at 8, boot_id u64 at 12 |
| 0013 | Notify-only canonical sensor frame, fragmented with the eight-byte header defined above |

CCCD subscription writes are SDK-managed; no application characteristic supports
writes. Identity is immutable within boot, so long reads are stable. Status fits
the default read payload. Telemetry contains measurement uptime, quality, source
identity, boot/revision/generation and typed sensor values. The service is currently
read-only without pairing/encryption; do not use it for confidential observations
or describe it as a secure production transport. Production BLE privacy/security
and bonding storage remain review items. Existing 0001/0002 diagnostics are unchanged
and are not registered by this service mode.

### Opt-in build and private development setup

Activate the exact SDK/toolchain from firmware/README.md first, and supply an
actual clean pinned kernel checkout with --kernel if this ZIP has an empty
submodule directory. Neither build command flashes or resets hardware.

```console
python tools/target/qualify_kernel.py build --runtime-port --configuration-app --static-net-queues --network-services ble --work-dir C:/uthome-builds/ble-services-new --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

For HTTPS, create a private JSON outside the repository, referencing an existing
certificate chain and matching unencrypted key. The certificate must name the
IP/DNS origin used by the client; mDNS is not implemented here. Example **field
shape only** (replace placeholders privately, never commit actual credentials):

```json
{
  "ssid": "YOUR_AP",
  "password": "YOUR_WIFI_PASSWORD",
  "token": "YOUR_RANDOM_32_TO_64_CHARACTER_DEVICE_TOKEN",
  "certificate_file": "device-cert.pem",
  "key_file": "device-key.pem",
  "port": 8443
}
```

Paths are relative to that JSON. SSID <=32 printable ASCII bytes; password 8..63;
token 32..64 visible ASCII bytes with no whitespace. Port 1..65535. PEM files
are each <=8191 bytes. Syntax/size checks are not certificate/key validation;
ESP-TLS validates key material during session initialization. There are no shipped
credentials, generated default certificates or verification-disable switches.

```console
python tools/target/qualify_kernel.py build --runtime-port --configuration-app --static-net-queues --network-services both --network-settings C:/private/uthome-network.json --work-dir C:/uthome-builds/network-services-new --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

`wifi` selects Wi-Fi/HTTPS only; `ble` selects BLE only; `both` requests coexistence,
which remains unqualified. The builder rejects diagnostic/native-UART/model mode
mixes and missing private inputs before build side effects. It snapshots owned
sources, generates a formatted private netsetup.h and verifies original private
inputs plus generated bytes on every direct rebuild. Changed inputs require a
fresh build. **The build directory, generated header, ELF and image contain secrets;
keep them private.** No actual credentials or PEM material were supplied or generated
in this implementation run. Do not publish a configured image or its source snapshot.

This is immutable development provisioning, not a runtime configuration endpoint.
Runtime profiles and persistence still belong exclusively to cfgctrl. HTTP mutations
remain unavailable. A production provisioning/rotation design through the existing
controller is still required. Never insert network fields into profiles that the
existing validator does not support.

After deliberate installation of a reviewed image, configure a real sensor profile
through the existing USB Desk. Obtain the assigned address from the AP/DHCP lease;
this code does not configure a PC gateway. For a certificate with the correct IP SAN:

```console
python tools/target/network_client.py https://192.168.1.50:8443 status --ca C:/private/device-ca.pem
python tools/target/network_client.py https://192.168.1.50:8443 telemetry --source 0 --ca C:/private/device-ca.pem
```

Set UTHOME_TOKEN privately before client use. The client verifies certificate and
hostname and refuses redirects; do not disable those checks. BLE clients connect
to UTHOME-SENSOR, read 0011/0012 and subscribe to 0013; the existing BleReassembler
decodes notifications using the actual negotiated MTU and resets on disconnect.

### Verification and remaining gates

Host SDK-boundary tests compile actual c3wsta/c3https/c3gsvc with C99 warnings as
errors and explicit SDK I/O doubles. They exercise association/IP loss/reconnect,
attempt cancellation, TLS WANT results, socket capacity, handshake expiry, partial
response writes, exact cleanup, GATT reads/write rejection, subscription, notify
busy retry, disconnect/re-advertise and MTU change. c3net is separately compiled
for wifi-only, BLE-only and both feature definitions. Private setup tests cover
staging/hash mutation, input bounds and invalid mode rejection. These doubles
do not establish pinned SDK ABI compatibility or TLS/RF operation.

Run with clang-format 20.1.8 installed or BARR_CLANG_FORMAT set to its executable:

```console
python -B -m unittest discover -s tests/target -p "test_network_*.py" -v
python -B -m unittest discover -s tests/target -p test_qualification.py -v
python tools/c_standard/check.py
```

Whole-image memory/stack/IRAM budgets have not been approved or increased. New
module reservations are intentionally not inserted into network-budget.json;
its unknown-module/budget failures must be resolved with actual maps and Member 1
review, never bypassed. TLS per-client heap, radio/lwIP/BLE packet storage, startup
objects, task stacks, cache-off/flash-save traffic, CPU time and stack high-water
measurements remain open. Before hardware installation: fresh real target build,
review of resulting RAM limits/SDK config, board identification and retained backup,
then the existing acceptance procedure. Build success alone is not permission to
claim static compliance or combined-system acceptance.

Manual C review: all new modules pair headers with C99 implementations. Only
owned SDK wrappers and app composition changed; no new language/ABI deviations.
Socket lengths, PEM sizes, source indices, notification lengths and byte casts
are bounded. sockaddr conversion is the SDK's compatible address API; SDK callback
types are used without casts. No mutex/interrupt guard spans TLS, driver I/O,
allocation or delay. Callback flags use the existing single-core saved-mask guard;
large buffers and netble delivery remain owner-only. Failed startup retains SDK
ownership rather than reinitializing leaked partial state; failure never calls
configuration mutation or stops local devices. SDK allocations remain visible.



## Latest-checkout port and light telemetry (2026-11-01)

The portable services and ESP bindings are ported into this checkout without
changing the existing runtime, queue/event/startup compatibility implementations,
SDK pins, driver registry or admission limits. Application and CMake hooks are
additive; the loaded-model and provisional light diagnostics remain separate.
Normal firmware still does not advertise the provisional SEN0390 driver.

Canonical frame v1 remains 112 bytes. Type 5 is light: offset 76 is unsigned
little-endian millilux (0..200,000,000 inclusive); offsets 80..111 are zero.
Zero is a valid reading. Missing/fault readings have all 36 value bytes zero;
fresh/stale semantics and instance/revision/generation/boot metadata are unchanged.
An out-of-range successful light reading is encoded as invalid/fault, never
truncated. Older clients must reject unknown types; the supplied decoder supports
1..5. This encoding is shared by HTTPS and BLE, including minimum-MTU fragmentation.

Run networking tests with `python -B -m unittest discover -s tests/target -p
"test_network*.py"`. The light tests decode actual C output, exercise zero/max/
out-of-range readings, missing/stale/fault states, minimum-MTU fragmentation,
and reject out-of-range/unused bytes in clients. No new C exception is introduced.
Manual review: the light branch performs a checked uint32 bound before explicit
four-byte serialization into the existing zero-initialized frame; no shared
sample layout, allocation, task, lock or driver activation changes. Existing
ownership and callback contracts are preserved. Target/host verification is
separate from physical sensor/radio and flash-save acceptance.

The local helper `../.esp-env/build-ble-latest.ps1` (relative to this repository)
activates the existing pinned environment and builds only this checkout. It does
not access serial ports, flash or reset a board. Use a fresh build directory for
each source revision. Historical build records in the previous checkout apply
to their original source snapshots only.

Verified latest-checkout build: `C:/uTesp-builds/ble-light-20261101-02`.
The BLE-only configuration application built with the pinned SDK/compiler/kernel;
its application binary is 645,040 bytes (38% of the 1 MiB partition remains).
All 72 networking and 36 qualification-tool tests passed; the C gate checked
315 files with zero findings or tool/policy errors. Fresh immutable
[build evidence](../../qualification/esp32c3-ble-light-services-20261101-build.json),
[memory review](../../qualification/esp32c3-ble-light-services-20261101-memory.json)
and [test/protected-source record](../../qualification/esp32c3-ble-light-services-20261101-tests.json)
are retained. Aggregate linker budgets fit unchanged, but the memory review
remains failed for missing reservations: c3gsvc 480 bytes and c3net 66 bytes.
No limits were added or increased. Runtime heap/stack, physical BLE/light and
flash-save/coexistence acceptance remain open. No board was flashed or reset.

## MQTT broker publisher implementation (2026-11-02)

`netbroker` now drives a real MQTT 3.1.1 clean-session publisher through one
bounded TLS I/O interface. `c3mqtt` binds that interface to pinned ESP-TLS and
`c3net` runs it from the existing controller loop when private MQTT settings are
present. No task, timer, SDK shim or configuration Apply/save implementation is
added. The existing fixed queue/event/startup compatibility work is included;
its open task/notification/timer, allocation and flash-cache gates stay open.
The SDK TLS client still allocates; this is experimental, not static qualified.

The optional `mqtt` object in the existing private development settings JSON
requires exactly `address`, `server_name`, `port`, `user`, `password`, `ca_file`.
Address is a numeric IPv4 literal (avoids network DNS in the controller);
server_name is the certificate/SNI name, 1..253 ASCII letters/digits/dots/hyphens;
user/password are each 1..64 visible ASCII bytes, port 1..65535. CA PEM is bounded
to 8191 bytes plus NUL. Keep JSON/CA/ELF/bin private; no settings values are
logged. The existing immutable-input manifest additionally hashes the broker CA.
Omitting `mqtt` leaves the broker service disabled. This is development-only
immutable provisioning, not persistent runtime credential management.

TLS requires CA-chain and certificate-name validation, with no plaintext or
skip-verification fallback. An async connection step may poll socket readiness
for up to 1 ms; the session owns a 30 s overall TLS/CONNECT/CONNACK deadline.
TLS crypto/allocation CPU time is not a measured scheduling guarantee. Certificate
validity-time support depends on the pinned TLS configuration and a trustworthy
clock; time/provisioning qualification is still open. No SNTP trust bypass is
introduced. Broker errors never invoke controller mutation or stop local work.

Implemented wire contract (the earlier production draft remains a proposal):

| Topic | Payload | QoS / retained |
|---|---|---|
| `uthome/v1/{node_id}/availability` | ASCII `online`; Last Will ASCII `offline` | 0 / yes |
| `uthome/v1/{node_id}/telemetry/{device_id}` | Canonical 112-byte sensor frame v1, types 1..5 including light | 0 / no |

Client ID is the MAC-derived node ID. CONNECT has username/password, clean session,
30 s keepalive and a retained QoS0 Will. A successful clean-session CONNACK must
be exactly `20 02 00 00`; rejection, session-present or malformed responses close
the session. No subscriptions, commands, QoS1 delivery, PUBACK queue or status
publication is implemented. Availability is best effort, not proof of sensor
health. The draft's JSON/QoS1 availability is not implemented and must not be
claimed by clients. Telemetry's boot/revision/generation/sequence fields distinguish
restarts and source identities.

State is OFF -> TLS -> CONNECT -> ACK -> AVAILABLE -> READY. Transport callbacks
perform at most one read/write/connect operation per step; MQTT buffers are
caller-owned. Two 1280-byte packet stores retain one active write and one pending
newest packet. Replacements saturate a counter; invalid offers preserve pending
state. Offline offers are dropped, not retained. IP loss, EOF, TLS failure,
malformed broker data or plan generation change closes the connection, drops
active/pending data and schedules capped retry backoff with jitter. A partially
written old-plan packet is never resumed under the new generation. Failures
saturate a counter. Backoff is 1/2/4/8/16/30 s; stable 60 s resets it.

One sensor position is captured per second while ready, rotating across the eight
plan positions, including absent positions. Capture uses `shared_source`, never
queue consumption; metadata is owned by the same controller. Zero/stale/fault and
light bounds are identical to HTTP/BLE. No backlog is replayed after reconnect.
Partial writes expire after 10 s. PINGREQ is queued after 15 s without completed
outbound traffic; one PINGRESP is required within 10 s. Unsolicited inbound
packets are rejected. Partial CONNACK and PINGRESP have fixed 4/2-byte storage;
there is no broker-controlled length allocation.

Reproducible host tests: `python -B -m unittest discover -s tests/target -p
"test_network*.py"`. New cases compile actual C and cover protocol bytes,
fragmented CONNACK, partial writes, credential rejection, malformed responses,
unsolicited input, keepalive success/expiry, broker EOF/outage/backoff, connection
and write deadlines, plan changes, clock regression/overflow, and partial TLS
cleanup on IP loss. Existing tests cover mailbox capacity/exhaustion, HTTP/BLE
concurrency and malformed input. SDK doubles are not TLS/RF runtime evidence.

For an explicitly configured test broker, use a verified-TLS MQTT subscriber with
an ACL restricted to this node's topics. Capture one telemetry payload as binary
(no newline/text conversion), then decode with `tools.target.network_client.decode_frame`.
For example, a Python test client can pass `message.payload` directly to that
function. Subscribe separately to availability as text. Check light type 5 has
integer millilux and that telemetry boot/generation changes after plan replacement.
Disconnect the broker/AP and verify bounded recovery plus local sampling on hardware;
these physical tests have not been performed. Do not install build-only fixture
images: their credentials and certificate are intentionally nonproduction.

Manual BARR-C review: new code is C99 with paired headers and bounded uint32/size_t
conversions. CONNECT fields are validated before encoding; the largest handshake
fits existing 1280-byte TX storage. MQTT remaining-length/control bytes and light
frames are explicit, never raw native structs. Only the SDK wrapper receives the
existing source-scoped external-header assembly spelling adaptation. Single-owner
contexts avoid additional locks/tasks; no guard spans TLS. Callback byte counts
are checked against requested capacity. Partial startup/EOF close the TLS client
exactly once; clocks/deadlines reject wrap. Topic/credential/CA inputs are bounded
and no secret logging, plaintext fallback, NVS or controller mutation is added.
Linked memory, runtime peaks, TLS CPU time and flash-save coexistence still need
review; the unchanged resource contract must not be bypassed.


Local reproducible build helper: `../.esp-env/build-network-latest.ps1 -Mode both
-Settings <private-settings.json> -BuildDirectory <fresh-build-directory>`.
Use `-Mode ble` without settings for the BLE-only image. This helper builds only;
it never connects to the broker or a serial port. A combined build using the
nonproduction fixture verifies active SDK compiler/link interfaces, not successful
association, verified TLS handshake or broker exchange on the ESP.

MQTT client callback example, with a separate verified-TLS test client:

```python
from tools.target.network_client import decode_mqtt

def on_message(client, userdata, message):
    observation = decode_mqtt(message.topic, message.payload)
    print(observation)
```

The decoder bounds topics and sensor bodies, checks topic/device identity, and
accepts availability only as exact `online`/`offline` bytes. The test client is
optional monitoring software; all actual publisher/network services run on ESP.


The combined TLS/BLE debug build exceeded the unchanged 1 MiB app partition.
A size-optimization trial (`-Os`) was reverted because it inlined the shared
boot mutex helper and failed the existing flash-placement check. The current
network build preserves the original debug optimization profile; neither shared
sources nor placement checks were changed. Trial optimization/map records are
historical analysis, not qualification of the current execution context. The read-only HTTPS adapter now reserves a 512-byte
reply and 128-byte RX chunk; two clients and all parser/authentication bounds
remain unchanged. Maximum-width identity/status fields, binary telemetry and
error routes are tested against the reply bound. This saves 1664 static bytes
without changing the controller or shared resource ledger. Any future endpoint
must prove it fits or introduce a reviewed streaming/buffer contract.


Combined MQTT/HTTPS/BLE remains blocked by the existing 1 MiB application
partition: the size-optimized combined image is still too large. The partition
and two-slot configuration storage layout have not been moved or enlarged.
Wi-Fi-only MQTT/HTTPS qualification is separate from combined coexistence.
Any release feature/flash-layout decision requires storage/runtime coordination;
no successful host test or partial ELF link overrides the partition size check.

The Wi-Fi service mode also retains the existing `c3bport_enter` link anchor,
so the SDK-facing archive resolves the native bridge when BLE is disabled.
This is a service-link selection change; the bridge and shared compatibility
implementations are unchanged.


### Final Wi-Fi-only build and resource handover

The final pinned SDK/kernel/compiler build at
`C:/uTesp-builds/mqtt-wifi-20261102-10` passed the full target qualification,
including the unchanged boot-helper flash-placement check and partition-size
check. The application binary is 913,536 bytes. It uses generated, nonproduction
build-only settings and must not be installed as deployment firmware. MQTT/TLS,
HTTPS and Wi-Fi service symbols are linked; BLE startup is absent in this mode.

All 86 networking tests, 36 build-tool tests and the 319-file C gate passed.
Artifact hashes and the final source snapshot are verified. Existing shared
runtime/HAL/core sources and the resource-budget file remain byte-identical.
Fresh [build evidence](../../qualification/esp32c3-mqtt-wifi-20261102-build.json),
[memory review](../../qualification/esp32c3-mqtt-wifi-20261102-memory.json) and
[test/source checks](../../qualification/esp32c3-mqtt-wifi-20261102-tests.json)
are retained. No board or live broker was accessed.

All aggregate fixed-memory ceilings fit: application 155105/155648 bytes,
SDK/kernel 57735/65536, RAM code 56278/57344, total DRAM 269118/278528, RTC 60/128.
The memory review still fails missing approved per-module reservations; it is
not a static-system pass. Wi-Fi service objects require c3https 3090 bytes,
c3mqtt 3087, c3net 68 and c3wsta 51, plus unmeasured SDK TLS/connection runtime
peaks. Review reservations and their sum with Member 1; do not insert or raise
limits just to pass. Peripheral-worker stacks and the shared pools are unchanged.

The combined size-optimization trial is retained as
[failed build evidence](../../qualification/esp32c3-mqtt-both-20261102-build.json)
and [preliminary map analysis](../../qualification/esp32c3-mqtt-both-20261102-memory-analysis.json).
That trial linked 1170864 bytes, exceeding the unchanged 1 MiB app partition by
122288 bytes; its optimized map is not qualification of the final debug profile.
Combined services, shared task/timer/allocation semantics, real broker/TLS and
AP-loss behavior, secure clock/provisioning, BLE security and flash-save/load
acceptance remain open. Authenticated network mutations still remain disabled
pending existing-controller USB/HTTP arbitration. No second Apply/save exists.


## Read-only network diagnostics and client test pack

`GET /v1/status` now includes an additive `mqtt` object under the existing bearer
and HTTPS authentication. No new unauthenticated endpoint or mutation operation
is introduced. Maximum-width fields still fit the existing 512-byte reply; the
384-byte JSON scratch buffer and 1024-byte request-header limit are unchanged.
BLE identity/status/telemetry wire formats are unchanged.

```json
{"enabled":true,"state":5,"failures":0,"replaced":2,"discarded":1}
```

| Field | Implemented meaning |
|---|---|
| enabled | MQTT was configured for this build; not proof of a successful start or connection |
| state | 0 offline/backoff, 1 TLS, 2 CONNECT write, 3 CONNACK wait, 4 retained-online write, 5 ready, 6 unavailable/uninitialized |
| failures | Saturating session-failure count, including IP loss, TLS/EOF/protocol errors and timeouts; intentional plan-generation resets are excluded |
| replaced | Saturating count of pending telemetry frames superseded by newer offers; now retained across disconnects |
| discarded | Saturating count of unsent active/pending telemetry canceled at reset, plus valid offers explicitly attempted while offline |

Counters are per boot and are never cleared by observation. The actual owner
skips capture while offline, so discarded does not count every sensor sample
produced during an outage. Sequence gaps also include intentional downsampling.
None of these counters acknowledges QoS0 broker delivery. A sample may have been
accepted by the socket but lost later. Read phase and counters alongside boot,
revision, generation, sample quality and availability. Disabled is distinguished
from configured-but-unavailable by enabled; phase is sampled by the controller
before that iteration's MQTT work and may lag one iteration.

`netbroker_snapshot` and `c3mqtt_status` only copy single-owner state. They perform
no I/O, allocation, reset or controller operation. Replacement history survives
mailbox clearing. On reset, only telemetry is counted as discarded, not CONNECT,
PINGREQ or availability control packets. Counters saturate at UINT32_MAX.

### Finite client captures

`tools/target/network_acceptance.py` is an optional PC observer/test client, not a
gateway. It never reads USB, applies configuration, publishes MQTT messages,
changes broker settings, flashes or resets a board. Live subcommands contact only
explicitly supplied endpoints when the operator runs them. No live endpoint was
contacted during this implementation; automated tests use socket/BLE doubles.

Run from the repository root in the prepared Python environment. Create an
output directory first, choose a configured sensor source index and set secrets
locally in process environment variables. Do not put tokens/passwords in command
history, screenshots or reports. Use trusted CA files and certificate names.
Each report path must be new; existing reports are never overwritten.

```powershell
New-Item -ItemType Directory -Path build/network-observations -Force
python -m tools.target.network_acceptance --help

# UTHOME_TOKEN must already be set locally.
python -m tools.target.network_acceptance --report build/network-observations/https-01.json https https://DEVICE_NAME:8443 --ca DEVICE_CA.pem --source 0

# UTHOME_OBSERVER_USER and UTHOME_OBSERVER_PASSWORD must already be set locally.
python -m tools.target.network_acceptance --report build/network-observations/mqtt-01.json mqtt --address BROKER_IPV4 --server-name BROKER_CERT_NAME --ca BROKER_CA.pem --node NODE_ID --count 3 --timeout 30

# Optional bleak must be installed separately in the PC client environment.
python -m tools.target.network_acceptance --report build/network-observations/ble-01.json ble --address EXPLICIT_BLE_ADDRESS --count 3 --timeout 30
```

Replace every uppercase endpoint/file/identity placeholder. HTTPS captures
identity, status and one source through the existing verified client, checks
matching boot/revision/generation, and validates diagnostic types/state codes.
An absent source, older firmware missing diagnostics, stale boot/plan capture or
transport error fails the capture rather than claiming acceptance. HTTPS uses
three bounded reads with the existing ten-second socket inactivity timeout.

The MQTT observer uses verified TLS and a numeric IPv4 address, a separate SNI/
certificate name, a unique observer client ID and clean session. Give its account
read-only ACLs on this node's availability and telemetry topics. It sends only
CONNECT/SUBSCRIBE/DISCONNECT, never PUBLISH. Capture is limited to 1..32 messages,
1..120 seconds overall, 1154 bytes per broker body and a bounded control-packet
count. Fragmented replies and retained messages before SUBACK are supported;
missing/rejected SUBACK, unexpected node/QoS, oversized/noncanonical packets and
unknown payloads fail closed. Credentials are absent from reports/error output.

The BLE observer reads the 44-byte identity and 20-byte status, checks boot
identity, then subscribes only to telemetry. It retains at most 32 frames within
an absolute 1..120-second timeout, stops notifications and resets reassembly on
exit. The backend may resolve the explicit address during connection; there is
no automatic board selection. It does not request pairing or prove BLE security.
`--mtu` is only a decoder override, not an ATT MTU negotiation request. Use it
only when the actual negotiated MTU is known; default uses the backend's value.
Missing bleak produces a failed report, not an automatic installation.

Reports distinguish captured/failed and always retain whole_system_qualified=false
and firmware_identity_verified=false. The network identity does not prove the
installed ELF hash. Record that hash separately through the existing approved
bench workflow. A successful capture may contain stale/fault/zero readings; it
is not proof of combined hardware, persistence, timing, security or memory
acceptance. Exception reports contain only error class, never arbitrary remote
error text. Ctrl-C does not fabricate a successful capture.

### Reproducible acceptance matrix

Software-only checks can be repeated now, without equipment:

```powershell
python -B -m unittest discover -s tests/target -p "test_network*.py"
python -B -m unittest discover -s tests/target -p test_qualification.py
python tools/c_standard/check.py
```

| Case | Reproduction / evidence | Current boundary |
|---|---|---|
| MQTT status and counter retention | Offline C tests inspect before/after replacement, partial send, generation reset, EOF and repeated snapshot; compare context bytes | Automated, no broker/ESP |
| Bounded HTTP and authentication | C tests exercise malformed/oversized messages, invalid credentials, separate clients, exhaustion and maximum-width diagnostic JSON | Automated SDK doubles; real sockets still need bench tests |
| MQTT client bounds | Offline observer tests fragment packets, deliver retained data before SUBACK, refuse auth/subscriptions, inject foreign topics/QoS and oversized/noncanonical lengths | Automated socket doubles; TLS cryptography and broker interoperability not established by doubles |
| BLE decode/lifecycle | C/client tests cover min/max MTU fragmentation, invalid frames, callbacks, boot identity, bounded capture and subscription cleanup | Automated doubles; no phone or RF accessed |
| Positive observation | Run the explicit capture commands against the selected, authorized test image/broker; record ELF, profile, boot, generation and source quality | Future bench work; commands have not been run live |
| AP/broker loss and reconnect | On an isolated approved setup, capture status before/after deliberate loss and restore; retain failure counts, availability timing and local sensor/display progress | Requires operator/network coordination; the clients never change AP/broker state |
| Minimum ATT MTU and disconnect | Use a client/backend that actually negotiates MTU 23, subscribe, disconnect/reconnect and start a new bounded capture | A decoder override alone does not qualify physical MTU behavior |
| Connection exhaustion/concurrency | Coordinate two partial HTTPS clients and a third attempt, concurrent MQTT/BLE traffic where an image fits, then release all clients; record recovery and allocation/object counters | Physical/resource acceptance remains open; no automated flood is run |
| Flash save under network load | Use the existing USB/Desk owner for an approved Apply/save while observing networking/local samples; record active/saved revisions, error/rollback results, timing and allocation/stack peaks | No save, flash interruption or electrical test was performed here |

Do not infer hardware acceptance from an offline PASS or the current read-only
capture reports. Preserve the existing hardware guide, board backups, deferred
radar test and separate authorization for installing/testing an image.

### Resource handover for Member 1

The following are actual linked fixed reservations, not approved module limits
and not runtime heap peaks. No new application task/queue or lock is introduced
by these diagnostics; SDK service allocations remain unqualified. Do not count
shared peripheral-worker stacks or queue pools twice.

| Service object | Wi-Fi-only bytes | BLE-only bytes |
|---|---:|---:|
| c3https | 3090 | not linked |
| c3mqtt | 3095 | not linked |
| c3net | 84 | 82 |
| c3wsta | 51 | not linked |
| c3gsvc | not linked | 480 |

Wi-Fi mode still has two HTTPS slots, one MQTT TLS client, two bounded MQTT
packet stores, a 512-byte HTTPS reply and 128-byte RX chunk. BLE mode still has
one peripheral connection and bounded active/pending telemetry. These fixed
sizes do not bound SDK TLS/NimBLE/lwIP allocation or task-stack peaks. Member 1
must review per-module limits, their total, SDK tasks/timers/notifications,
post-startup allocations and flash-cache behavior; no approval is inferred.

The fresh Wi-Fi diagnostics build reserves application 155129/155648 bytes,
SDK/kernel 57735/65536, RAM code 56278/57344, total DRAM 269142/278528, RTC 60/128.
BLE-only reserves application 149371/155648, SDK/kernel 40122/65536, RAM code
55078/57344, total DRAM 244571/278528, RTC 60/128. Aggregate ceilings fit, but
memory reports remain failed for unapproved service module entries. Runtime heap,
stack headroom, client exhaustion and flash-save behavior still need measurement.
Existing shared runtime/HAL/core sources and the budget file are unchanged.

### Flash-size analysis and coordination options

`tools/target/network_size.py` reads linked artifacts/partition tables and ranks
mapped Flash Code/Data by archive. It validates qualified artifact hashes and
never changes a partition, storage location, budget, source or acceptance result.
Use a new report path:

```powershell
python -m tools.target.network_size --build C:/uTesp-builds/network-diagnostics-20261102-01 --output build/network-observations/flash-size-01.json --top 10
```

The diagnostics Wi-Fi binary is 913936 bytes, leaving 134640 bytes in the unchanged
1 MiB app partition. The historical combined debug reference is 1272912 bytes,
exceeding that partition by 224336 bytes. That reference predates diagnostics;
these are separate source contexts, not a controlled A/B measurement.

Largest mapped-flash contributors in that combined reference include the BLE
controller archive (241823 bytes), Wi-Fi net80211 (138028), crypto tfpsacrypto
(129255), lwIP (121520) and BLE host libbt (93558). These are archive attributions,
not exact removable savings; image headers, padding, IRAM load copies, shared
code and dependencies matter. Shrinking BSS buffers improves RAM, not this code
size problem. The earlier size-optimization trial failed shared helper placement
and was reverted; that gate was not relaxed.

Current coordination options are to retain separate validated Wi-Fi/BLE images,
review a release feature set through controlled builds, or review a larger app
partition with the storage owner. A partition change must account for actual
flash capacity, existing configuration data, backup/migration and recovery; it
must not silently move the two-slot configuration store. No such change is
implemented. Shared compatibility fixes,
trusted clock/credential provisioning and BLE security remain separate open work.

Manual C review of diagnostics: all additions remain C99 with paired headers,
fixed uint32 wire counters and explicit enum conversion. Counter additions are
at most two and saturate before overflow. Snapshot calls are owner-only, copy
bounded fields, allocate nothing and do not reset/consume data. Internal fields
are appended; BLE/sample/storage wire layouts remain explicit and unchanged.
The maximum-width JSON test preserves the fixed 512-byte reply without a new
buffer reservation. No lock spans formatting or SDK work, and no kernel/SDK
source, language deviation, formatter, warning, placement or budget gate changed.

Fresh evidence: [Wi-Fi build](../../qualification/esp32c3-network-diagnostics-wifi-20261102-build.json),
[Wi-Fi memory](../../qualification/esp32c3-network-diagnostics-wifi-20261102-memory.json),
[BLE build](../../qualification/esp32c3-network-diagnostics-ble-20261102-build.json),
[BLE memory](../../qualification/esp32c3-network-diagnostics-ble-20261102-memory.json),
[software/source checks](../../qualification/esp32c3-network-diagnostics-20261102-tests.json),
[Wi-Fi size attribution](../../qualification/esp32c3-network-diagnostics-20261102-size-wifi.json) and
[combined reference attribution](../../qualification/esp32c3-network-diagnostics-20261102-size-combined-reference.json).
The 99 networking tests, 36 qualification-tool tests and 319-file C gate passed;
Wi-Fi and BLE-only target builds passed with pinned dependencies. No board,
live broker, BLE adapter or serial port was accessed.

## Authenticated configuration Apply and USB arbitration (2026-11-03)

Wi-Fi firmware now binds HTTPS to `cfgmux`, which calls the existing serialized
`cfgctrl_handle(CFGWIRE_APPLY)`. Validation, activation, two-slot persistence and
recovery remain the existing controller operations. There is no second Apply/save
implementation. BLE and MQTT remain telemetry-only. HTTP supports full-profile
Apply with persistence; standalone Save, recovery, reset and provisioning remain
unsupported. A missing controller binding makes configuration operations unavailable.

Every endpoint requires the existing HTTPS Bearer credential. POST requires exact
`Content-Type: application/json`, canonical decimal `Content-Length` 1..8192,
`X-Request-ID` (1..UINT32_MAX), `X-Revision` (1..UINT32_MAX),
`X-Expected-Revision` (0..UINT32_MAX), `X-Expected-Boot-ID` (16 lowercase hex),
and `X-Request-Token` (1..64 ASCII letters/digits or `._-`). Duplicate transaction
headers, leading zeros, signs, overflow, transfer encoding and malformed framing
are rejected. Content encoding is unsupported (415), Expect is unsupported (417),
oversize bodies return 413. The body is the complete existing node JSON, with
its original schema, device/resource validation and persistence behavior.

GET `/v1/configuration` returns version, boot_id, last_request_id, active_revision,
saved_revision, durable_known, recovery, busy and max_body. Choose a global ID
above last_request_id, a new revision and the observed active revision/boot as
preconditions. A concurrent USB request can invalidate these values: return 409,
inspect state and resolve the conflict explicitly. USB IDs are never rewritten.
State and result reads do not consume IDs or evict the controller cache.

Only one HTTP upload may borrow the existing idle USB receiver's 8192 bytes.
Partial USB frames, retained unread bytes or pending USB replies prevent admission
(503). While leased, application USB parsing pauses; USB backpressure can cause a
host timeout and require resynchronization. Local sensor/display workers continue.
There is no request queue, second profile buffer, new task or new lock. Two TLS
clients, 128-byte RX chunks and the 512-byte response reservation remain unchanged.
The existing ten-second receive deadline bounds headers plus upload. Disconnect,
AP loss, timeout or malformed upload releases the lease without invoking Apply.
Only a complete admitted body reaches the controller. A coalesced excess body is
rejected before dispatch; subsequent bytes cannot create a second request.

Completion JSON has version, boot_id, request_id, controller_status,
active_revision, saved_revision, active_crc, saved_crc, flags and historical.
Saved completion returns 200, validator rejection 422, stale/conflict 409, and
storage/recovery/active-but-unsaved outcomes 503. Pre-controller errors do not
consume an ID; requests dispatched to cfgctrl follow its existing ID/cache rules.
The owner refreshes network revision metadata immediately after Apply and before
further sensor observations. The response-send deadline starts after controller
work, rather than charging synchronous validation/flash work to socket sending.

GET `/v1/configuration/result` requires X-Request-ID and X-Request-Token and returns
only the latest retained matching HTTP completion (404 otherwise). It does not
read flash or rerun Apply. `historical` means another controller request has since
advanced the global ID; completion fields are facts at completion, not fresh
state. Compare a new state query before interpreting current revisions.

An exact POST retry is permitted only while that HTTP request still owns the
controller's latest cached body: all metadata and every body byte must match.
It returns the retained outcome without validation/save side effects. A USB
request can evict this POST retry cache (409), while the historical result GET
remains available until a later HTTP completion. Tokens/results are bounded,
RAM-only and per boot. Uncertain saves remain uncertain; no automatic retry,
new-ID replay or reboot recovery is inferred. After a lost reply query the result,
then state; use the existing USB recovery procedure when required.

Resource review: Wi-Fi fixed application RAM is 155297/155648 bytes (351 bytes
headroom), SDK/kernel 57735/65536, RAM code 56278/57344, total DRAM 269310/278528,
RTC 60/128. This adds 168 fixed application bytes to the previous diagnostic
build. BLE-only fixed totals are unchanged. Aggregate ceilings fit, but unchanged
module budgets still reject unapproved c3https/c3mqtt/c3net/c3wsta entries (BLE:
c3gsvc/c3net). Member 1 approval, SDK heap/task peaks, flash-save scheduling and
real concurrent-client/USB behavior remain unqualified. Combined Wi-Fi/BLE still
exceeds the existing app partition; separate images remain the supported builds.

Software checks: 123 network tests, four USB regressions, 36 qualification-tool
tests and the 321-file pinned-format C gate passed. Tests exercise actual cfgctrl,
cfgpump and cfgstore with fake storage plus HTTPS SDK doubles: malformed/8KiB
bodies, auth, preconditions, overlapping uploads, USB arbitration, uncertain save,
exact retry, cache eviction, disconnect and timeout. Host admission fixtures use
a small strict JSON schema; production retains the full cfgdisc validator.
Manual C review checked borrowed-buffer lifetime, release paths, subtraction-first
bounds, owner serialization, preserved wire enums, exact-byte retry comparison,
no lock across flash/SDK work, and no new coding exceptions. Real Wi-Fi and BLE
builds passed; hardware timing, TLS allocation and flash acceptance remain open.

See the target-tools guide for explicit client commands. Immutable build, memory
and test records use the `esp32c3-http-config-20261103` qualification prefix.

## Networking hardening and acceptance checklist

The fixed-RAM review retains the current wire limits and reservations. HTTPS owns
two independent 1024-byte header parsers, because both TLS clients may hold
partial headers; they cannot share storage while the upload token borrows parsed
headers. Its 512-byte reply must survive partial writes, while its 128-byte RX
chunk may receive another client. MQTT has independent active/pending packet
stores: the active packet must survive partial sends and the pending packet
implements latest-value replacement. Sharing these buffers across transports or
moving them onto the owner stack would change lifetime/stack requirements, not
establish a RAM saving. Controller/pump buffers are already borrowed for uploads.
No buffer sizes, connection counts, queues, shared code or budget limits were
changed by this review. A smaller HTTP header cap, one TLS client, or a smaller
MQTT packet contract needs a separately reviewed compatibility/resource decision.
Fixed RAM headroom is not available SDK heap or evidence of task-stack headroom.

The following is a reproducible bench checklist, **not completed hardware
acceptance**. Use a separate test device, trusted HTTPS credentials and a valid
full-node profile. Record firmware/evidence hashes, boot ID, original profile,
active/saved revisions and normal sensor/display update periods first. Coordinate
one global request-ID sequence between USB and HTTP. Use unique reports. Do not
run destructive storage fault injection on a valued board. The offline fake-store
tests cover uncertain-write paths without wearing or corrupting real flash.

| Scenario | Procedure | Required observation / evidence |
|---|---|---|
| Sensor baseline | Read each configured source over HTTPS/MQTT/BLE in the appropriate separate image, including light | Values and source IDs match local readings; record sample age and configured update period |
| AP loss/rejoin | Interrupt AP availability during telemetry, then restore it | Local sensor/display updates continue; Wi-Fi reconnects; no growing backlog; record counters and outage/recovery timestamps |
| Broker outage | Keep AP online, stop/restart broker | HTTP/local operation continues; bounded replacement/discard counts; telemetry resumes without replaying stale backlog |
| TLS capacity | Hold two handshakes/partial requests; attempt a third | Extra connection rejected/released; originals expire by their deadlines; local operation continues and slots become reusable |
| Slow upload | Send authenticated headers and incomplete body, adding bytes before each deadline | Whole upload still expires at ten seconds; no Apply/ID consumption; USB lease releases |
| USB already busy | Leave a USB frame partial or response pending, then POST | HTTP returns unavailable and never resets/overwrites USB state |
| USB during HTTP | Start partial admitted POST; queue a USB QUERY with the next ID; complete POST | One HTTP Apply, then USB progresses with unchanged IDs; record any host timeout/resynchronization |
| Two HTTP writers | Hold first upload partial; send another POST | Second returns unavailable; no queue/extra Apply; first lease remains valid |
| Oversize/framing | Use offline client tests or explicitly craft invalid lengths, duplicate metadata and malformed JSON | 413/framing rejection before controller, or 422 for dispatched invalid JSON; no activation/save for invalid input |
| Response loss | Close client after complete POST; query matching result, then state | Completion observable if retained; no automatic POST replay; historical result distinguished from current state |
| During real save | With benign profile changes, maintain telemetry reads and USB QUERY traffic | One serialized Apply/save; metadata coherent; record latency gaps, SDK allocation/stack peaks and local update continuity against Member 1's agreed bounds |
| Retry/cache eviction | Repeat identical retained POST, then advance global ID through USB QUERY | First retry has no new save; after eviction POST conflicts, historical result GET remains available until replaced |

The host doubles now also test a response after a 20-second owner-clock jump
following controller completion. This checks deadline accounting only; it does
not demonstrate that an actual flash save takes that long, or certify real-time
behavior during cache-disabled flash operations. A physical result must include
measurements and agreed limits rather than treating an HTTP 200 as acceptance.

## Integration with current system-integration

This contribution is merged onto GitHub system-integration commit
`cf94b46ac227f706dd1d3611760ba7db342e964d`, retaining its newer storage, service,
HAL-folder and build-mode refactors. Earlier build/memory records above describe
the ZIP-based source context; they are retained as historical evidence, not
qualification of this merge. Fresh merge records use `esp32c3-network-merge-20261103`.
Only the application composition is changed among existing firmware C/header
files; existing controller, USB pump, storage, shared runtime/HAL/network-core and
compatibility patch implementations are preserved from the remote branch.
Networking sources and test compiler paths follow the current layout.

Build selection extends the existing immutable `BuildSelection` configuration
mode with optional `--network-services ble|wifi|both`; it does not restore the old
positional build API. Wi-Fi/both require explicit private --network-settings;
BLE uses no private settings. Defaults and isolated diagnostic prerequisites are
preserved. The host fixtures use POSIX PIC flags and Windows-only DLL export/
unload options as appropriate; Linux CI execution is still a separate result.
No SDK checkout, compiler, generated settings, key or firmware binary belongs
in this source commit. Dummy PEM markers in fixture tests are not credentials.

The fresh merged Wi-Fi build is 920288 bytes and BLE-only is 646496 bytes.
Wi-Fi application fixed RAM is now 155305/155648 (343 bytes headroom), SDK/kernel
57735/65536, RAM code 56348/57344, total DRAM 269388/278528 and RTC 60/128.
BLE application fixed RAM is 149379/155648, SDK/kernel 40114/65536, RAM code
55156/57344, total DRAM 244649/278528 and RTC 60/128. Module approval failures
remain the same named service modules; no limits or compatibility gates changed.
Both builds and all 127 networking, 43 qualification/mode and 14 USB regressions
passed, with the 351-file C gate. See the qualification index's current-branch
merge records. No runtime/hardware or Linux CI qualification is inferred.

## Direct ESPHome occupancy (opt-in laboratory image)

Home Assistant connects **to the ESP32-C3 itself**. The application owner reads
the existing copied target report for device ID `presence_sensor` (the first
prototype profile). Any present slot in a fresh OK report means occupied;
zero present slots in a fresh OK report means clear. Missing, wrong-device,
busy, stale, and fault reports have no valid state. A disconnect makes the
entity unavailable. The service never opens UART, drains a sensor queue,
changes a configuration, or controls an output.

**Build and pair.** Use the private Wi-Fi settings file and pinned build command
above with `--network-services wifi`, a fresh work directory, and
`--static-net-queues`. Remove the optional `port` field and add the JSON field
`"esphome_lab": true`. Keep the existing private provisioning fields.
This option intentionally replaces HTTPS with a single read-only plaintext
ESPHome native API listener on TCP 6053; there is no Noise encryption or
authentication. Use an isolated trusted lab LAN only. No mDNS, MQTT broker,
proxy, or ESPHome installation on the board is needed. With the lab flag
omitted, HTTPS behaves as before. Use the established USB procedure to apply
the radar profile. In Home Assistant add the **ESPHome** integration manually,
enter the board IP and port 6053, and look for **Room occupancy**. Do not flash
an unqualified image over the team's known-good setup without its backup.

**Budget and acceptance.** One client; 128-byte RX, 128-byte TX, maximum
125-byte request body. Partial I/O retains offsets; oversize input drops the
client. Protocol state shares the existing 512-byte HTTPS reply reservation
(the compile-time size check rejects growth). No new application tasks, queues,
mutexes, semaphores or allocator calls. The temporary cache copy uses the
existing owner stack: verify its high-water mark. The lwIP socket path is
inherited and **not covered by the application-level storage proof**. Before
claiming AAI13, build the actual lab image with the pinned toolchain, compare
the map against the existing budgets, and count actual allocation and kernel
creation calls after startup during pairing, stalled connections, disconnects
and reconnects. SDK/lwIP/TCP objects are not automatically Wi-Fi-exempt.

Host regression tests compile the board adapter with warnings as errors and
exercise stale, fault, wrong source, oversize, fragmentation and reconnect.
The selected architecture uses this native API for display and MQTT for rule
requests/results. The API exposes radar entities and the general Transaction result text entity.
The MQTT session receives bounded rule commands and publishes results. Host
socket/TLS-double tests exercise both paths; they do not establish live board
MQTT/Home Assistant acceptance. See the [integration boundaries](../../../experiments/runtime_rules/README.md#integration-acceptance-remaining).
Install the pinned host client with `python -m pip install -r tests/target/requirements-esphome.txt`.
The mandatory CI loopback test checks both radar entities and drives the same C adapter with the real ESPHome client (only socket I/O
and radar input are doubled). Run `python -m unittest discover -s tests/target
-p test_network_esp.py -v` as one command from the repository root. On hardware,
confirm a person entering/leaving changes state, stopping radar updates gives
unknown rather than clear, and local sampling continues through a client reconnect.

**Radar data fresh** is a separate diagnostic binary sensor. It is on for a fresh valid target report (even an empty room), and off for missing, stale, faulty, or mismatched radar data. Both entities become unavailable when Home Assistant loses its connection. Both use the same copied snapshot, existing connection state and 128-byte transmit buffer.


## Application-owned transaction commands

The [three-protocol service](../../../experiments/runtime_rules/README.md#running-the-integrated-service)
extends the existing TLS MQTT session with a node-specific QoS0 subscription.
`netbroker_subscribe` enables the topic before connecting; `netbroker_request`
copies one 96-byte command to `c3tx`, which executes it through `txjob`/`txport`
only while the peripheral plan is idle. SUBSCRIBE/SUBACK use diagnostic state
values 7/8, preserving values 0..6. Reconnect repeats subscription; malformed,
retained/DUP/QoS or oversized input closes the session and clears queued input.
A partial packet has one 10-second absolute receive deadline.

Results use `/transaction/result`, with priority over ordinary telemetry. The
ESPHome laboratory endpoint now advertises a text sensor named Transaction result
(key 3), decoded by the pinned real client test. Results are copied and expire
after 30 seconds; this read-only endpoint never executes a transaction. See the
service guide for wire fields, fixed pins, exact-retry/unknown-outcome rules,
private provisioning and physical acceptance still required.

The static-control regression (`test_network_static_control.py`) compiles the
production `c3nq_static` ABI boundary with pool admission doubled and checks
pinned lwIP validity, untouched failed leases and null storage. A board boot
previously reproduced the `tcpip_send_msg_wait_sem` initialization assertion;
the corrected image boots and returns USB telemetry. The initialization marker
is an owned adapter compatibility fix; SDK/kernel sources remain unchanged.
