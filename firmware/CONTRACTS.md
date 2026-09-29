# Integration contracts

Selected 2026-09-28 for platform baseline 2.0.0. This document defines the small
contracts needed to integrate Members 1, 2, 3 and 5. It is a design agreement;
only the interfaces explicitly identified as implemented exist in this checkout.
Implementation checkpoints are tracked in [the plan](../INTEGRATION_PLAN.md).

Checkpoint 3 adds `i2cbus` transport/time, the `c3i2c` backend and the
`bmp388`/`bmpinst` driver adapter. These are software/build verified;
electrical acceptance is pending. Checkpoint 4a adds `spibus`, `pinpool`,
`digiout`, C3 SPI/pin adapters and the `ssd1306`/`oledinst` frame sink. I2C and
SPI share pin ownership; the runtime remains device-independent. Checkpoint 4b
adds UART/radar and checkpoint 4c adds GPIO input, pulse capture, distance and
generic PWM. Later checkpoints connect USB profile activation, telemetry and
[configured display rendering](src/services/DISPLAY.md); hardware acceptance
and actuator routing remain pending.
At the user's request,
physical checks are deferred to final hardware acceptance.
The appended health events DEVICE_IO and UART_RX_OVERFLOW have codes 6 and 7;
existing codes remain stable. Overflow counts observed loss epochs, not lost bytes.
Consumers must recompile the enlarged in-process health snapshot.

## Identity and validation

Keep node schema 2.0.0, descriptor/board/build versions 1.0.0 and normalized host
plan version 1.0.0. This checkpoint preserves their fields and meanings while
tightening admission of consuming-queue routes as described below.
`device_id` names an instance; `driver` names compiled code. A descriptor cannot
add executable functionality. Host and target must both validate a full candidate
against the actual build and board, including disabled instances' physical wiring.

Use the existing shared host planner. The firmware will parse the approved node
profile itself; neither an unchecked host plan nor a legacy AI recommendation
is a deployment contract. Preserve the current 8 KiB node-input limit, reject
duplicate keys/non-finite numbers and add bounded depth/allocation checks in the
target parser. The existing target-pending manifest remains empty.

## Copied sample records: implemented declarations

`include/uthome/sample.h` is the shared C sample contract, version 1. It has no
HAL, SDK or kernel dependency. Types describe data capabilities rather than
device models. Existing component callbacks and the message bus are unchanged.

| Catalogue queue item type | Message type | C record | Values |
|---|---|---|---|
| `environment_sample` | `UTHOME_SAMPLE_ENVIRONMENT` (1) | `sample_environment_t` | Pressure in Pa, temperature in degrees C; finite floats |
| `target_report` | `UTHOME_SAMPLE_TARGETS` (2) | `sample_target_report_t` | Three slots: signed x/y in mm, signed speed in cm/s, resolution in mm, presence Boolean |
| `distance_sample` | `UTHOME_SAMPLE_DISTANCE` (3) | `sample_distance_t` | Distance in mm, echo duration in microseconds |
| `health_event` | `UTHOME_SAMPLE_HEALTH` (4) | `sample_health_t` | Existing `health_event_t` code and cumulative occurrence count |

Every record has the same header:

- `timestamp_ms`: 64-bit kernel monotonic time at measurement/event completion.
  USB rendering may expose this as uptime; it is not UTC. An adapter converts a
  driver microsecond timestamp only if it uses that same clock origin; otherwise
  stamp completion with the runtime clock. Do not use SDK startup log timestamps.
- `source_index`: the device's zero-based position in the active plan, including
  disabled entries. Resolve it to `device_id` for display and external telemetry.
  `UTHOME_SAMPLE_NO_SOURCE` is reserved for node-level health events.
- `sequence`: starts at zero per source when a plan activates and advances once
  per publication attempt, including error records and failed queue writes.
  Unsigned wrap is modulo 2^32. It is not a persistent identity or global counter.
- `status`: unknown (0), success (1), timeout (2), I/O failure (3), invalid data
  (4). Zeroed records are unknown. Drivers must map HAL/component/kernel results
  explicitly; their numeric codes are not interchangeable with sample status.

Consumers use measurements only on success, then apply their freshness limit.
On failure publish an error record with zeroed measurements and the error status;
do not relabel a previous successful measurement with a fresh timestamp. Absent
target slots are cleared and marked not present. A valid health-event record
uses success status even when the event describes a node fault.

Queues copy records by value using `sizeof(record)`, with a 64-byte item ceiling.
The contract tests enforce that ceiling at C99 compile time and exercise the
actual message bus. Native structure padding is allowed; never transmit or save
raw struct bytes. USB/storage serializers must write explicit fields. These
declarations are not driver implementations or a USB wire protocol.

Adding another driver that produces one of these formats needs no new sample
type. A genuinely different data format gets its own type ID and consumer
support. Do not expand these records for every device-specific setting.

## Ownership, routing and bounded work

| Resource | Owner / rule |
|---|---|
| Active configuration and lifecycle calls | One long-lived application control task |
| Driver context and binding | Distinct storage per instance, retained until checked cleanup and task deletion succeed |
| I2C/SPI controller | Shared HAL bus with serialized finite transactions; never one duplicate controller per device |
| UART controller | One owning instance; reuse the selected kernel driver, with no second RX owner |
| Queues and health state | Application lifetime; serialized through short target-side synchronization |
| Sample buffer | Producer until publish returns; queue holds its own copy |
| Consumer | Consumes a copied record; never retains a pointer into a queue or another driver's context |

Existing `component_operations_t` configure/read/write/publish callbacks remain
the functional interface. Each registry adapter maps complete validated settings
and sample formats explicitly. Operation success remains zero; a sample's status
is separate. Use the existing `rt_port_worker_t` step/cleanup callbacks rather
than adding another generic lifecycle interface.

A step performs a finite unit of work; the configured timeout covers the whole
operation, not a fresh timeout for each byte or retry. Report timeout/I/O failure
in data and health. The worker step returns nonzero only when execution must stop;
recoverable device errors can return zero after reporting, under a bounded retry
policy. A heartbeat demonstrates completed bounded work, including a reported
failed I/O attempt; it is not proof the peripheral is healthy or a timer-only ping.

Cleanup runs after the worker is confirmed dormant, tolerates partial setup and
is safe to retry. Release only successfully released resources, disable outputs
to their defined safe state, and quiesce interrupt callbacks before freeing their
context. Cleanup failure retains ownership. Shared buses close after their last
owned instance is released. Admission and binding perform no hardware I/O.

The current message bus is a consuming FIFO, not broadcast: multiple consumers
would compete for entries. Permit one enabled consuming service per configured
queue; the host planner now rejects competing consumers and repeated source
references. A disabled consumer retains its wiring reservation but does not
consume queue entries. Target admission must enforce the same routing rule.
Monitoring will use copied latest-value snapshots, not steal display queue items.
Multiple producers retain their source indices; a consumer of such a queue must
process each source rather than treating `consume_latest` as latest-per-source.
The typed target plan now rejects conflicting declared consumer masks. Complete
descriptor-to-callback routing and the monitoring cache remain pending.

No queue or health lock may be held across driver I/O, a task-stop wait or another
blocking lifecycle operation. Interrupt handlers capture bounded data and signal
work; they do not parse profiles, configure drivers or call the current unlocked
message-bus API. Target tasks now use `src/runtime/shared.h`: a short
nonblocking guard serializes each operation. On `UTHOME_PORT_ERR_BUSY`, no
operation ran; retry later without spinning. Raw queue/health/watchdog calls
must not run concurrently with these wrappers. Shared reset is owner-only and
requires all worker resources to have been released.

## Whole-plan activation and deployment

The periodic target implements full replacement in
[`activate`](src/runtime/ACTIVATION.md), including when only one interval
changes. The application still needs complete profile admission and real driver
adapters before using this sequence with hardware:

1. Validate the entire candidate without changing the running plan.
2. Retain the previous validated settings; stop and quiesce all affected workers
   and consumers. Confirm cleanup before releasing context or bus ownership.
3. Clear queues, latest-value caches and watchdog registrations while quiescent.
   This permits simple plan-local indices; old samples cannot cross into a new
   identity mapping. The initial implementation replaces the whole plan.
4. Prepare the new resources, configure drivers and create dormant workers.
   Keep execution behind an activation barrier until all preparation succeeds.
5. Activate and check startup. On failure clean up the partial plan and rebuild
   the preceding plan; if that fails, expose recovery state and block dispatch.
6. Save the last-known-good profile only after successful activation.

Do not require two sets of worker stacks or peripheral handles at once. Keep the
prior configuration data so resources can be recreated. Rollback restores a
working configuration, not old sensor samples, queue contents, counters or past
physical effects. Failure to stop a worker prevents reusing its resources.

USB will process one bounded request at a time. The external result must identify
the request and active configuration revision and distinguish `received`,
`validated`, `active`, `saved`, `rejected` and `recovery`. A save failure after
activation reports that the running revision differs from the durable revision;
it must not claim successful persistence. Duplicate requests must not silently
repeat activation. Reconnect queries retrieve active and saved state.

Checkpoint 5a now defines transport framing and storage commit markers; the
parser allocation limit and production adapters follow in the next integration
steps. The complete path must pass combined tests before deployment is claimed.
There is no physical transport endpoint in this checkpoint. Recovery must retain the previous complete saved profile and
remain accessible over USB if no valid profile can be activated.

## Resource ledger and release gate

Keep the current bounds: eight configured instances, four peripheral workers,
four queues, depth eight, 64-byte items, and 16 KiB total peripheral stacks.
Disabled instances reserve wiring and metadata but no active worker stack.

| Memory category | Accounting rule / current evidence |
|---|---|
| Peripheral stacks | Sum admitted active stack requests, at most 16,384 bytes; dormant prepared workers also consume the actual port budget |
| Queue payload pool | Current `msgbus.c` statically allocates 4 x 8 x 64 = 2,048 bytes regardless of configured depth/count |
| Queue metadata | Checkpoint 2 C3 object symbols: queue table 2,240 bytes, including 2,048-byte payload and 192-byte metadata |
| Driver and HAL contexts | Account actual bounded pools and buffers once per instance/bus, including any vendor-owned buffers |
| Parser and candidate plans | C3 activation module retains two 1,704-byte typed plans plus 96-byte slots, 28-byte status and two Boolean bytes (3,534 symbol bytes before linker alignment). Caller candidate storage is additional; parser/raw-profile storage remains pending. |
| Fixed services and kernel | Account control/USB/health/idle stacks, kernel objects and platform buffers separately from peripheral stacks |
| Telemetry caches | Account each retained copied sample and metadata; USB/frame buffers must not be counted twice |
| AI reserve | Select a reserve after the full application link map and high-water measurements; unmeasured RAM is not an AI allocation guarantee |

The planner's existing `queue_payload_bytes` means configured logical queue
demand (depth x 64), not allocated RAM. Keep that field's meaning unchanged.
Its resource totals are partial admission checks, not a whole-application memory
certificate. Do not fabricate numeric driver/parser/service budgets before those
implementations exist. At target acceptance, record the linker map, allocations,
stack high-water marks and worst activation/recovery peak without double-counting
static pools already included in the map. Target capability limits must be
reduced if the measured implementation cannot support the current bounds.

## UART stream adapter

The current LD2450 adapter maps a stream workload to an explicit 10 ms periodic
worker, processing at most 64 bytes per step. Idle polls publish nothing; each
batch publishes only its latest complete typed report. Timeout measures time
since valid input/start; malformed traffic cannot extend it. Faults discard
partial data and publish zeroed error records. Native event-driven readiness,
profile binding/admission and USB telemetry remain later integration work.
See [the concrete contract](drivers/hlk_ld2450/INTEGRATION.md).

## Pulse capture and generic PWM

`pulseio` owns a trigger/input pair with a total acquisition deadline;
`hc_sr04` applies sensor-specific conversion and an acoustic quiet period after
completion. `hcinst` publishes typed distance or zeroed fault records through
existing queues. `pwmio` owns an output with zero-duty setup/cleanup; `pwminst`
accepts copied duty commands through the generic component API. Component
wrappers map callback failures to `UTHOME_COMPONENT_ERR_OPERATION`; direct
HAL/driver calls retain their own status codes.

The C3 implementation provides two capture pairs and four independent PWM
channels, subject to shared pins. A later binder must allocate those resources
explicitly; generic catalogue maxima do not override backend limits. Command
routing, arbitrary GPIO event callbacks and physical acceptance remain pending.
See [ownership, timing and manual review](drivers/hc_sr04/INTEGRATION.md).

## Configuration core implementation (checkpoint 5a)

Framing, request sequencing and two-slot persistence are now implemented as
portable C99 modules, with the byte layout and callback contract in
[TRANSFER.md](src/config/TRANSFER.md). Size remains 8192 profile bytes; the wire
header is additional. Request results distinguish validation/activation/save
milestones and active/durable revisions, including uncertain saves.

Checkpoint 5 is split for review: 5a is this core and simulated storage failures;
5b adds bounded managed-cJSON parsing, semantic/resource admission and binding;
the subsequent integration connects real USB and persistent storage. These
parts must be exercised together before declaring deployment complete. No raw
profile is currently wired to production activation. Target capabilities stay
empty until the appropriate integration and physical acceptance pass.

## Production admission and binding (checkpoint 5b)

The [admission contract](src/config/ADMISSION.md) now defines bounded JSON,
backend restrictions, three sensor bindings and initialization order. Use
cfgbind_operations for production cfgctrl callbacks. USB/storage remain pending.
Generic plan resources configure before worker allocation and release after all
instance cleanup/task deletion; failure retains ownership. activate_clear
provides empty RECOVER semantics, while activate_recover restores previous
settings. OLED binding followed at 6b; bounded rule/PWM admission follows at 7. Include
parser/binder memory in the full application budget before reserving AI RAM.

## Configuration endpoint (checkpoint 5c)

The optional application mode owns native USB and the dedicated `uthome_cfg`
partition through portable pump/controller/store contracts. PROBE command 5
discovers the last request ID without consuming it or evicting the retry cache;
QUERY refreshes active/saved state. See
[USB/storage contract](src/config/USB_STORAGE.md). Hardware acceptance remains
pending, and the target-pending manifest remains conservative.

## Observation service update (checkpoint 6a)

Target `shared_publish` now records successful, registered typed samples in a
bounded per-source cache and counts queue drops under the same guard. Monitoring
uses `shared_capture`; it does not remove display/rule queue entries. The binder
copies source identities/freshness limits before worker release and restores
them through plan rollback. See [telemetry/health semantics and wire layout](src/services/TELEMETRY.md).
Do not additionally run the legacy raw queue-health observer for these queues.

## Local application and rule contract (checkpoints 6c/7)

CAPABILITIES command 7 describes compiled bindings separately from physical
qualification and preserves mutation request/retry state. The local configurator
uses this response, the existing planner and the USB client; it submits the
reviewed node document for independent target validation.

Schema 2.1 retains 2.0 sensor/display input and adds typed rules with one command
owner per enabled PWM output. Rules select exact source IDs through the shared
cache without consuming FIFO entries. The output worker owns its rule state and
uses the existing component command/cleanup paths. See [bounds, timing, zero
fallback and manual review](src/automation/README.md). Hardware acceptance and
AI are deferred, and the target-pending manifest remains empty.
