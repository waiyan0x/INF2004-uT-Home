# Telemetry, health and OLED display

Read-only observations share typed samples with the OLED and rule services.
Configuration changes remain owned by the controller.

## Data ownership and freshness

`shared_publish` copies successful publications into `sampcache` under the same
nonblocking guard as the queue write. It does not remove items. Display/rule
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
BUSY/errors retry on a later owner iteration; there is no spinning under the
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
160..1824 bytes (184..1848 including framing), written in <=48-byte nonblocking
attempts. The existing 2000 ms whole-reply deadline starts at the first transmit
step. Partial replies can be discarded after disconnect/expiry without changing
configuration. The client uses a bounded buffer and overall deadline, validates
correlation/size/CRC, and never automatically retries a mutation.

All fields below are explicitly little-endian. Error words use signed int32
two's-complement interpretation. Native structure padding never crosses USB.
The 160-byte payload header contains 40 uint32 words:

| Byte offset | Field |
|---|---|
| 0, 4 | Payload version 1; result 0 success, 1 busy, 2 capture error |
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

With application firmware installed and no other USB client connected:

```powershell
python -m tools.config_transfer telemetry --port COMX
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
