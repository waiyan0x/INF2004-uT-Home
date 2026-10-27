# Telemetry, health and OLED display

Read-only observations share typed samples with the OLED and rule services.
Configuration changes remain owned by the controller.

## Data ownership and freshness

`shared_publish` copies successful publications into `sampcache` under the same
short guard as the queue write. The kernel wrapper can yield before acquisition
to let another task release it; see [shared operations](../runtime/README.md#shared-operations).
It does not remove items. Display/rule
consumers retain the existing single-consumer routing contract. Generic queue
payloads and unregistered sources are ignored by the cache, preserving msgbus's
general contract. Direct raw msgbus writes intentionally bypass observation;
target producers must use `shared_publish`.

The cache has eight device slots, indexed by active-plan position (including
disabled positions), and four global health slots, one per queue. Registration
copies source name, type, destination and freshness limit before worker release.
Only enabled bound producers are registered. Two producers sharing a queue keep
separate slots. A configured source with no publication appears as `missing`.
An empty device list is distinct from a device successfully measuring zero.

Quality values are 0 missing, 1 fresh OK, 2 stale OK, 3 fault/unknown. Only fresh OK
values are eligible for live decisions. An older OK value remains visible
as stale for diagnosis. The freshness bound is twice the worker period plus its
I/O timeout and 100 ms, matching the current heartbeat bound. Data becomes stale
at that bound. A timestamp is measurement completion, not USB receive time.

Equal/older sequence numbers do not refresh the cache. Ordering uses modulo-2^32
forward differences 1..0x7fffffff, with nondecreasing timestamps. Sequence wrap
works; jumps of half the sequence space require a plan reset. Future timestamps,
non-finite environment values, unknown statuses and malformed absent target
slots become INVALID with zero values. Error/unknown samples also have zero
values. Native sample objects must obey `sample.h`, including valid bool object
representations; this API does not parse hostile raw bytes as C structures.

Plan replacement, rollback reconstruction and successful clear reset caches,
queue counters, health and watchdog registrations. Rollback restores copied
source metadata and the previous health interval, not old measurements. Consumers
must associate observations with runtime generation and active revision; neither
generation nor request IDs are a persistent boot identifier.

## Health supervision

`cfgbind_service` runs in the existing owner loop while the runtime is ACTIVE.
`statusmon` checks registered worker deadlines every 100 ms, independently of USB
polling. A missed heartbeat is counted once until that worker next makes progress.
This diagnoses progress; it neither restarts tasks nor proves a sensor is healthy.
The last service call's error appears in telemetry and can return to zero after a
retry. Per-sample status and cumulative health counters retain separate meaning.

The profile's `health_report_interval_ms` now controls publication to every
configured health queue. Reports contain the last historical event and that
event's cumulative count, source 65535 and report-completion time. No event is
fabricated when counters are empty. Status OK means a valid report, never an
assertion that the node is well. The snapshot also supplies the original event
time and all eight cumulative/suppressed counters. Event time remains the
existing modular uint32 clock; report/snapshot/sample times are uint64.

Every actual queue drop is counted inside `shared_publish`, including reject-newest
drops. Do not run the legacy raw `hlthbus` observer on these same queues or the
same drops would be counted twice. Undrained queues, including health queues,
can therefore produce real overflow events during software-only operation.
After bounded guard acquisition attempts, BUSY/errors retry on a later owner
iteration; there is no spinning under the
guard. A busy publication can leave one health destination ahead of another;
reports describe individual publication attempts, not an atomic broadcast.

## Wire contract

Command **6 TELEMETRY** uses the existing UTH1/version-1/24-byte header and CRC32.
Request ID is a nonzero correlation value; revision and payload length are zero.
Response command is 0x86, revision zero. Unlike controller commands 1..4, this
command consumes no ID and does not evict the last mutation's retry cache. It
performs no flash access. Command 5 PROBE remains unchanged.

Observation replies have variable-length payloads. Controller/probe replies remain
48 bytes. The pump accepts a <=2048-byte observation payload; this format uses
352..2016 bytes (376..2040 including framing), written in <=48-byte nonblocking
attempts. The existing 2000 ms whole-reply deadline starts at the first transmit
step. Partial replies can be discarded after disconnect/expiry without changing
configuration. The client uses a bounded buffer and overall deadline, validates
correlation/size/CRC, and never automatically retries a mutation.

All fields below are explicitly little-endian. Error words use signed int32
two's-complement interpretation. Native structure padding never crosses USB.
Payload version 3 has a 352-byte header. Its first 160 bytes retain the version-1
layout below except that the payload version is now 3. The host decoder also
accepts version 1 (160-byte header) and version 2 (320-byte header); absent audit data is `null`,
never evidence of zero allocations. The framing version remains 1.

The first 160 header bytes contain 40 uint32 words:

| Byte offset | Field |
|---|---|
| 0, 4 | Payload version 3 (legacy 1/2 accepted); result 0 success, 1 busy, 2 capture error |
| 8, 12 | Snapshot monotonic time, low/high halves |
| 16, 20 | Active/saved revision |
| 24, 28 | Active/saved profile CRC |
| 32 | Controller state flags: durable-known 16, recovery 32, active-known 64 |
| 36, 40 | Runtime state (empty 0, active 1, recovery 2), generation |
| 44, 48 | Failed workers, retained instances |
| 52 | Last service-call error |
| 56, 60 | Sample row count <=12, queue row count <=4 |
| 64, 68 | Total health events/suppressed notifications |
| 72, 76, 80 | Has-last-event (0/1), event code, original modular event time |
| 84 | Health event count (8) |
| 88..119 | Eight event counts |
| 120..151 | Eight suppressed counts |
| 152, 156 | Runtime apply/recovery errors |

Versions 2/3 append a fixed allocation-audit header:

| Byte offset | Field |
|---|---|
| 160 | Flags: available 1, startup sealed 2, saturated 4; all other bits zero |
| 164 | Reserved zero |
| 168, 172, 176 | Startup successful SDK allocations, cumulative requested bytes, frees |
| 180, 184, 188 | Same counters after startup is sealed |
| 192..235 | Eleven startup kernel create-attempt counters |
| 236..279 | Eleven normal-operation kernel create-attempt counters |
| 280..287 | Reserved zero |
| 288..303 | Four startup kernel allocator entry-attempt counts |
| 304..319 | Four normal-operation kernel allocator entry-attempt counts |

Version 3 additionally distinguishes application startup from earlier SDK/kernel
boot without clearing the original counters:

| Byte offset | Field |
|---|---|
| 320 | Application entry observed (0/1); must be 1 when sealed |
| 324, 328, 332 | Successful SDK allocations, hook byte total, frees during application startup |
| 336..351 | Four kernel allocator entry-attempt counts during application startup |

Application-startup counters are a subset of total startup. The marker is set
at the start of `usermain`, before the idle task or controller initialization.
It ends at seal, before restore. Older captures expose this subset as `null`;
they cannot prove that application startup was allocation-free.

Create-kind order is task, mutex, semaphore, event flag, mailbox, message buffer,
fixed pool, variable pool, rendezvous port, cyclic handler, alarm handler.
Missing/failed providers leave the entire audit extension zero. An unsealed snapshot cannot
contain normal-operation counts. Counts saturate with a sticky flag instead of
wrapping. They persist through profile changes; reset starts a new boot.
Requested bytes are cumulative allocation traffic, not live usage or a peak.
The SDK callbacks observe successful heap events, not failed attempts. Create
counters observe attempts regardless of native success. These measurements do
not independently prove stack safety or cover other allocators. See the
[counter boundary and review](../runtime/README.md#allocation-and-object-counters).

An unsuccessful capture sends only the header with version/result and all other
bytes zero. Do not interpret that zero-filled failure as an empty healthy node.
On success, queue rows precede sample rows. Each 32-byte queue row has eight words:
index, sample type, published, consumed, dropped, current depth, high-water depth,
reserved zero. Queue order is configuration order.

Each sample row is 128 bytes:

| Byte offset | Field |
|---|---|
| 0, 4, 8, 12 | Queue index, sample type, source index, quality |
| 16, 20, 24, 28 | Sample time low/high, sequence, sample status |
| 32, 36 | Freshness limit ms; age ms saturated at UINT32_MAX |
| 40..71 | Copied source name, ASCII/NUL-padded |
| 72..111 | Values, unused bytes zero |
| 112..127 | Reserved zero |

Missing rows have zero sample time/sequence/status/values and age UINT32_MAX
(shown as null by Python). Types retain `sample.h` IDs: environment 1 has two
binary32 values (pressure Pa, temperature Celsius); distance 3 has two uint32
values (distance mm, echo microseconds); health 4 has uint32 event/count.
Target type 2 uses three 12-byte slots: word 0 is present bit 0 and resolution mm
in bits 16..31; word 1 contains signed-int16 x/y mm in low/high halves; word 2
contains signed-int16 speed cm/s in the low half. Unused bits and absent slots
are zero. Endianness, binary32, lengths, names, padding and state bounds are
checked by the decoder. Target measurements remain signed.

Shared samples, queues, counters and time are copied in one guard. Runtime and
controller metadata are sampled nearby by their common owner; workers are not
frozen across all three operations. The state ACTIVE denotes an accepted plan,
not successful measurements. The plan cannot change during encoding.

## OLED rendering

`shared_sample` copies one registered source and its time reference under the
existing short shared guard. Selection rotates every two seconds through all
sources on the configured queue mask, including multiple producers on one queue.
Rendering and SPI happen after releasing the guard. No queue item is consumed.
The current single-consumer admission reservation remains conservative; rendering does not change the planner's routing policy.

Each page shows the device index (or health queue), a shortened uppercase label,
and MISSING/FRESH/STALE/FAULT. Values include pressure/temperature, distance/echo,
all three radar slots with signed coordinates/velocity, or historical health
event/count. Health reports do not certify current node health. Missing/faulted
values clear old numbers; an unavailable snapshot shows WAITING FOR DATA.
The built-in 5x7 alphabet has 41 glyphs; unsupported label characters are blank.
Temperature uses one truncated decimal; telemetry retains the original values.

The binder opens shared SPI only for enabled displays, then claims each CS,
D/C and reset through existing ownership checks. Its worker renders directly
into that instance's frame and calls the existing bounded flush/retry adapter.
Only that worker writes the frame. Stopped-worker cleanup releases the driver,
control lines, SPI device, then shared bus. Partial opens and failed releases
retain handles for checked recovery; admission does not touch live resources.

The display context occupies the existing fixed slot union. This uses more
static memory even for sensor-only profiles, but keeps one ownership model and
avoids a separate frame allocator. Historical ELF sizes are recorded in qualification evidence, not a free-RAM guarantee.

## Read a snapshot

Workload observation is a separate read-only command, **9** (`WORKCOST`), with
zero request revision/payload. It neither consumes mutation IDs nor evicts the
last mutation reply. `Client.workcost()` decodes it; existing telemetry and the
Configuration Desk retain their current wire contract and appearance.

Both version-1 and version-2 payloads are 384 bytes: a 96-byte header and four 72-byte permanent
worker records. All words are explicitly little-endian uint32, except the
timestamp stored as consecutive low/high words. Failure is a 96-byte header
with its selected version, result 1 (busy) or 2 (unavailable), and every other byte zero.
Version 2 is selected only when a scheduler-counter provider is installed.

| Header byte | Meaning |
|---|---|
| 0 / 4 | Version / result (0 = complete) |
| 8 / 12 / 16 | Runtime generation / active revision / active profile CRC |
| 20 | Four permanent slots |
| 24 / 28 | SDK monotonic microsecond timestamp, low/high; distinct from kernel uptime |
| 32 / 36 / 40 | Controller reservation 8448 / untouched prefix / measured flag |
| 44 / 48 / 52 | Idle reservation 1024 / untouched prefix / measured flag |
| 56 | Observer wall duration in microseconds, including stack scans and encoding |
| 60 | Version 2: scheduler observation available, 0 or 1; version 1: zero |
| 64 / 68 | Version 2: native monotonic milliseconds, low/high |
| 72 / 76 | Version 2: cumulative idle-attributed milliseconds modulo 2^32 / tick period in ms |
| 80..95 | Reserved zero |

| Record byte | Meaning |
|---|---|
| 0 / 4 / 8 | Physical slot / configured source index / nonreused binding handle |
| 12 | Flags: bound = 1, costs available = 2, stack measured = 4 |
| 16 / 20 | Permanent 8192-byte reservation / minimum untouched prefix since boot |
| 24..64 | Eleven `workcost_stats_t` counters, in declaration order |
| 68 | Reserved zero |

Unbound records have zero source, handle and cost fields. Unavailable costs have
zero wire counters but decode to `None`, never measured zeros. Source indices
retain disabled entries in the admitted profile. Binding handles/generation
prevent differencing across replacements. Cumulative costs reset on a dormant
slot's next binding; stack minima persist across bindings. Saturated counters
are explicit and cannot label calibration windows. The host separately binds
the complete verified ELF and profile SHA-256; wire CRC alone is not provenance.

Worker elapsed time includes waits/preemption. Bus wait covers acquisition;
active covers the synchronous transfer and release call, not electrical wire
time. Successful payload excludes I2C address/framing bits and discarded UART
bytes. UART `AGAIN` is an expected nonblocking result. Controller-side setup and
discovery transfers are excluded. Completion lateness uses the kernel clock at
1 ms resolution, converted to microseconds; SDK and kernel epochs are never
compared. No metric claims CPU busy time. Each cost snapshot is coherent; the
nearby owner metadata/stack reads are not a simultaneous hardware trace.

Version 2 reads `tk_get_otm` and the permanent idle task's `td_inf_tsk` under one
saved/restored interrupt guard, without clearing either counter. The native
timer attributes each 1 ms tick to the current task. Interrupt work can therefore
be attributed to idle or another interrupted task; this is sampled scheduler
activity, not cycle-accurate CPU or isolated ISR utilization. A provider failure
sets availability zero and leaves bytes 64..79 zero; worker costs remain useful.

`WorkcostReply.scheduler_window(previous)` accepts two available observations
with matching revision, CRC and binding generation, increasing SDK timestamps,
and a native window of 100 ticks or 100 ms (whichever is longer) through 60 s.
It handles one 32-bit idle wrap, rejects resets/inconsistent deltas and reports
`sampled_non_idle_percent = 100 * (elapsed - idle) / elapsed`. The caller must
still verify the same connection and ELF identity. A single snapshot cannot
yield utilization. The original workload measurement/export contract and frozen
AI study features remain unchanged. The Desk displays no new metric without
its separate UI review.

Manual C review of this extension: fixed caller-owned callback storage, checked
native task/clock results, nonnegative high-word conversion, bounded unsigned
shifts and modulo idle arithmetic. The permanent idle ID is published before
normal observation and never rebound. No allocation, object creation, counter
reset, blocking wait or I/O occurs inside the observation guard. Target fixture
checks increasing native time and idle attribution; C/Python parity covers both
versions, missing providers and a high-word clock. Host window checks cover wrap,
short/long windows, changed profile/generation, reset and malformed availability.

The measurement helpers and wire path are implemented in source; see current
qualification records before treating an installed image or dataset as qualified.

With application firmware installed and no other USB client connected:

```powershell
python -m tools.config_transfer telemetry --port COMX
python -m tools.config_transfer workcost --port COMX
```

Replace COMX with the actual board port. Exit 0 means a decoded successful
capture, 2 a device capture failure, 1 a transport/decoding failure. A valid
capture can still contain stale/fault samples. This command does not query
storage, deploy, reset the board or consume mutation IDs.

OLED pages rotate every two seconds. Basic BMP388/OLED/radar bench observations
are in the [qualification index](../../qualification/README.md#peripheral-bench-observations).
SPI writes cannot prove a panel is present. Combined-load, disconnect, recovery
and resource measurements still require physical acceptance.


## Manual BARR-C review

- Rules 1/5: new services and transport changes use C99, fixed-width wire values,
  unsigned shifts and justified range conversions. Binary32 is checked; memcpy
  transfers float bits and native aligned samples without pointer aliasing.
  No C11 scope, SDK-header dialect or approved exception is broadened.
- Rules 1/7: cache ownership shares the existing nonblocking guard. No raw shared
  call is nested, no lock spans I/O, and encoding uses a static copied snapshot.
  Static owner contexts avoid large task-stack locals. Settings copied by the
  activation layer preserve metadata/intervals through rollback. No heap/new
  task/ISR/MMIO is introduced, and no worker's borrowed storage is freed.
- Rules 2/4/6: paired headers, service prefixes, explicit callback typedefs,
  bounded tables and documented call order separate observation from mutation.
  Driver-specific binding remains in cfgbind; services dispatch sample types.
- Rules 3/8: full pinned formatting and lexical gate, active assertions, checked
  frame sizes and bounded iterations. Error/unknown values cannot be mistaken
  for OK; first sample, stale time, wraparound and cache-reset paths are reviewed.

Host tests cover shared-queue source isolation, missing/stale/error/future data,
wraparound, nondestructive capture, health deadlines, plan rollback/clear,
fragmentation, invalid frame sizes/CRC, partial TX expiry and unchanged mutation
retries. Real C bytes are decoded by Python. Compiler warnings remain errors;
both real target modes are built. Build/static memory evidence cannot replace
combined-load timing, USB disconnect or physical sensor/flash acceptance.

## Display C review

- Rule 1: C99, bounded and documented casts, paired types at callback boundaries.
- Rule 2: accurate ownership, rendering limits and error/retry contracts.
- Rule 3: pinned formatting and the complete lexical gate.
- Rule 4: one cohesive renderer, paired header/prefix and private helpers.
- Rule 5: fixed-width samples/masks, unsigned bounded shifts, matching integer
  printf macros and finite range checks before float-to-integer conversions.
- Rule 6: bounded functions/loops and explicit worker/lifecycle ownership;
  no added allocator, recursion, ISR or synchronization framework.
- Rule 7: initialized buffers/handles, stable callback contexts, copied samples
  and preserved ownership after failures; stopped workers precede union reuse.
- Rule 8: checked error paths, bounded formatting/glyph writes and active test
  assertions. The shared guard is released before formatting, GPIO or SPI.

The new module uses C99. Existing C11/GNU99 scopes, exceptions, approval,
checker policy, dependency pins and formatter version are unchanged. Software
review does not establish physical timing, stack peaks or complete compliance
with every semantic rule on hardware.

Dated build/test/static-size results: [telemetry](../../qualification/esp32c3-telemetry-20260928.json) and [display](../../qualification/esp32c3-display-20260928.json). These are historical measurements, not current free memory.
