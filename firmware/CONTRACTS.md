# Shared integration contracts

Shared sample, registry and lifecycle contracts for the configurable C3 platform.
Current implementation/status: [firmware guide](README.md). Module guides specify
actual binding limits. The health event codes DEVICE_IO=6 and UART_RX_OVERFLOW=7
remain stable; overflow counts loss epochs, not individual bytes.

## Identity and validation

Node schemas 2.0.0/2.1.0, descriptor/board/build 1.0.0 and normalized host
plan 1.0.0 retain their contracts; schema 2.1 adds typed output rules.
`device_id` names an instance; `driver` names compiled code. A descriptor cannot
add executable functionality. Host and target must both validate a full candidate
against the actual build and board, including disabled instances' physical wiring.

Use the existing shared host planner. The firmware independently parses the node
profile; neither an unchecked host plan nor a legacy AI recommendation
is a deployment contract. Preserve the current 8 KiB node-input limit, reject
duplicate keys/non-finite numbers with bounded depth/allocation checks in the
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
| `light_sample` | `UTHOME_SAMPLE_LIGHT` (5) | `sample_light_t` | Illuminance in integer millilux, 0..200,000,000; not yet bound to a device |

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
Monitoring uses copied latest-value snapshots without consuming queue items.
Multiple producers retain their source indices; a consumer of such a queue must
process each source rather than treating `consume_latest` as latest-per-source.
The typed target plan now rejects conflicting declared consumer masks. The binding layer supplies descriptor routing; `shared_publish` also updates
the per-source monitoring cache under the same guard.

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
[`activate`](src/runtime/README.md#application-contract), including when only one interval
changes. The production binder performs independent profile admission and resolves
drivers before this sequence:

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

USB processes one bounded request at a time. The external result must identify
the request and active configuration revision and distinguish `received`,
`validated`, `active`, `saved`, `rejected` and `recovery`. A save failure after
activation reports that the running revision differs from the durable revision;
it must not claim successful persistence. Duplicate requests must not silently
repeat activation. Reconnect queries retrieve active and saved state.

The [configuration guide](src/config/README.md) defines the implemented parser,
framing, activation and storage path. Recovery remains accessible over USB
when no valid profile can activate; RECOVER does not erase the saved profile.

## Resource ledger and release gate

Keep the current bounds: eight configured instances, four peripheral workers,
four queues, depth eight and 64-byte items. Active stack requests are admitted
against a 16 KiB aggregate limit, but the permanent four-by-8-KiB worker pool
reserves 32 KiB even when idle. Disabled instances still reserve wiring/metadata.

| Memory category | Accounting rule / current evidence |
|---|---|
| Peripheral stacks | Charge all four permanent 8 KiB stacks (32 KiB); the separate admitted-request cap is 16 KiB |
| Queue payload pool | Current `msgbus.c` statically allocates 4 x 8 x 64 = 2,048 bytes regardless of configured depth/count |
| Queue metadata | Account separately from the payload pool unless included in the reported object size |
| Driver and HAL contexts | Account actual bounded pools and buffers once per instance/bus, including any vendor-owned buffers |
| Parser and candidate plans | Fixed typed cJSON pools, binder candidate and retained activation plans; use the actual linked reservations |
| Fixed services and kernel | Account control/USB/health/idle stacks, kernel objects and platform buffers separately from peripheral stacks |
| Telemetry caches | Account each retained copied sample and metadata; USB/frame buffers must not be counted twice |
| AI reserve | Select a reserve after the full application link map and high-water measurements; unmeasured RAM is not an AI allocation guarantee |

The planner's existing `queue_payload_bytes` means configured logical queue
demand (depth x 64), not allocated RAM. Keep that field's meaning unchanged.
Its resource totals are partial admission checks, not a whole-application memory
certificate. Use linked sizes and measured peaks rather than assumed driver/parser/service
budgets. At target acceptance, record the linker map, allocations,
stack high-water marks and worst activation/recovery peak without double-counting
static pools already included in the map. Target capability limits must be
reduced if the measured implementation cannot support the current bounds.


## Module-specific contracts

- [Runtime](src/runtime/README.md): worker lifecycle, activation barrier, shared
  guard, rollback and retained resources.
- [Configuration](src/config/README.md): JSON admission, copied settings, USB
  requests, durable records and reconnect/recovery semantics.
- [Telemetry/display](src/services/TELEMETRY.md): per-source cache, freshness,
  wire observations and nondestructive rendering. Do not double-count drops
  with the legacy raw queue-health observer.
- [Automation](src/automation/README.md): exact source identity, one rule owner
  per output, hysteresis/cooldown and zero fallback. Rule workers use copied
  cache observations and the component write/cleanup path.
- [Driver index](drivers/README.md): UART polling, pulse capture, PWM and each
  sensor's electrical/timing limits. Backend limits override catalogue maxima.

Current build/bench status is in the [evidence index](qualification/README.md).
Full hardware acceptance and embedded AI deployment remain incomplete.
