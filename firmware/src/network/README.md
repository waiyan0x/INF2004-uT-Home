# Fixed network compatibility resources

This folder contains the owned queue, wait and resource-lease implementation
for the pinned ESP32-C3 networking compatibility layer. It is **experimental**:
the corrected SPI study includes the fixed core but does not activate radios.
Wi-Fi/BLE/MQTT/HTTP services and combined radio qualification remain open.

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
Their opaque control-buffer pointer is retained as the handle, never dereferenced.
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
flash saves before acceptance. Remaining task-stack/notification,
timer-callback, BLE allocation and lwIP/service paths are separate open work.
The vendor timer callback currently runs in task-independent context, whereas
[FreeRTOS specifies timer-service task context](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch06.md).

The [pinned source audit](../../qualification/esp32c3-wireless-source-audit-20261027.json)
records the exact files and remaining task/notification/TLS/timer gaps. It is a
source review, not proof that every path was reached in a radio test. Next work
is a fixed service-task/notification adapter, a permanent timer service with
bounded commands, and explicit NimBLE/lwIP/service storage. The current task
wrapper allocates stacks and creates native objects per request; notification
increment/no-overwrite/clear behavior and timer period/context semantics need
replacement. A blanket SDK heap exemption would not satisfy the requirement.

## Verification and manual BARR-C review

The host C tests use warnings as errors and actual compiled modules. They cover
25,000 seeded ring operations against a deque, one-slot/full/front/wrap/overwrite
behavior, buffer canaries, arithmetic limits, actual tick conversion, stale
wakeups, class exhaustion, static caller storage, blocked-release interleavings,
ISR polling, mutex ownership/recursion and failed-clock/reinitialization paths.
These host tests do not establish RF behavior, native scheduling or physical timing.
Fresh target build/board results must be recorded separately.

Manual review for BARR-C rules 1–8: owned code is C99 with paired module headers,
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

The selected queue/event/startup variant uses `tools/target/network-budget.json`:
278,528 bytes overall, split into 155,648 application data, 65,536 SDK/kernel data
and 57,344 RAM code. The older `netqueue-budget.json` belongs to the queue-only
experiment. Neither budget alone proves that radio services fit.

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

`c3boot` supplies a 12,288-byte, eight-byte-aligned native startup pool. This is
separate from the four 8 KiB worker stacks. Scoped USB-core evidence covers this
variant; changes still require boot, stack-margin and allocation verification.
It is not a general-purpose application heap or permission for later native
allocation. Failure to initialize is terminal until reset. The experimental
`tools/target/network-budget.json` redistributes the same 278,528-byte total:
155,648 application data, 65,536 SDK/kernel data and 57,344 RAM code. It is a
reservation to test, not evidence that the complete wireless system fits.

Manual BARR-C review (rules 1–8): these modules remain C99 with paired headers,
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

Manual BARR-C review (rules 1–8): `c3gatt` owns constant SDK definitions, one
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
