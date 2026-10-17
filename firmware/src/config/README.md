# Configuration, USB and storage

One controller owns the path from reviewed JSON to running devices:
`cfgpump → cfgctrl → cfgbind/activate`, with `cfgstore → c3store` for persistence.
The board independently checks every profile; a host-valid plan is not enough.
Runtime, framing and storage do not dispatch on prototype device names.

## Modules and ownership

| Modules | Responsibility |
|---|---|
| `cfgbytes`, `cfgwire` | Explicit little-endian fields, CRC and bounded framing |
| `cfgstore`, `cfgctrl` | Durable records, request identity, activation/save ordering |
| `jsonscan`, `cfgjson` | Bounded JSON parsing and exclusive cJSON allocation |
| `cfgplan`, `cfgdev`, `cfgrule` | Board/resources, driver/settings and typed-rule admission |
| `cfgbind`, `c3bind`, `cfgdisc` | Copied instance settings, actual transports and discovery/controller coordination |
| `cfgpump`, `c3usb`, `c3store`, `c3app` | USB pump, physical storage and application owner loop |

Initialize cJSON hooks and activation once, then one persistent binder/controller.
All large contexts have static/application lifetime. One long-lived kernel task
owns calls; reentry guards are not cross-task locks. Only `cfgctrl` may apply,
restore or clear live profiles. Out-of-band activation invalidates its metadata.
Context, scratch, input and response storage must not alias. Keep request bytes
stable until handling completes; retain unread RX and partially written TX bytes.

The application reuses the upstream 8 KiB initial-task stack at priority 20,
restores once, then services transport/health with a 1 ms yield. This yield is
not a bound on flash, activation or complete request latency. Build/install via
the [firmware guide](../../README.md) and [target tooling](../../../tools/target/README.md).

## Admission

- Node schemas 2.0.0 and 2.1.0; 2.1 adds bounded rules. Board/descriptor versions
  must be supported. Profiles cannot install drivers or firmware code.
- Exactly 1..8192 bytes, one root object, at most eight nested containers.
  Reject trailing content, NUL, non-ASCII, duplicate decoded keys, unknown fields
  and unsupported versions. No trailing terminator is required.
- Integer tokens are canonical decimal uint32, or int32 for signed thresholds:
  no fractions, exponents, negative zero or extra leading zeros. Host schema
  validation alone does not enforce all target lexical restrictions.
- Managed cJSON `1.7.19~2` has a 32 KiB allocation-request ceiling including
  aligned wrapper headers, and at most 1024 allocations per parse. Exhaustion
  rejects the candidate and frees partial trees. Allocator bookkeeping and
  stack use are additional. One owner exclusively uses cJSON/global hooks/locale.
- Check identities, driver/bus/settings/routes, queue types, single enabled
  consumers, shared producers, pin/address collisions and exclusive UART use.
  Disabled entries still reserve identity and wiring.
- Admit at most eight instances, four workers, four queues of depth eight,
  16 KiB of peripheral stacks, and priorities 5..19. Settings are copied into
  bounded scalar storage, never retained JSON pointers.

| C3 binding | Limit |
|---|---|
| Ordinary GPIO | 0, 1, 3, 4, 5, 6, 7, 10 |
| UART | Port 1, RX20/TX21, 512-byte buffer |
| I2C | Port 0, 10..400 kHz; BMP388 at 0x76/0x77 |
| SPI | Host 2, 100 kHz..8 MHz; no GPIO2 CS |
| Pulse capture | Two configured pairs, including disabled entries |
| Typed queues | Environment, targets, distance, health |

Production bindings cover BMP388, LD2450, HC-SR04, SSD1306 and PWM output.
Radar polls every 10 ms. OLED renders registered samples without consuming them.
Each enabled PWM output needs exactly one admitted [rule](../automation/README.md).
Compiled support is separate from hardware acceptance; the target-pending
manifest stays conservative. The original GPIO2 prototype is not deployable.

## Activation and recovery

Admission writes candidate scratch only. The binder owns scratch and eight
distinct instance slots; never reinitialize or overwrite retained live resources.
Shared buses open only for enabled users. Plan-level resources configure before
worker preparation and release after every worker/driver cleanup succeeds.
Failed configure also receives checked release; failed cleanup retains ownership.
Scalar settings fit the 128-byte activation slot. Stop and clean a slot before
changing its union member; rollback reconstructs previous settings/resources.

Callbacks map status explicitly: activation success, rejection with old plan
restored, or uncertain/recovery state. `activate_clear` releases to safe EMPTY;
`activate_recover` restores the preceding plan. Neither silently discards retained
resources. The controller's RECOVER does not erase the saved startup profile.

APPLY establishes durable state, validates, activates, then saves. Rejection
preserves active/saved state; uncertain activation blocks APPLY/SAVE until
recovery. Save failure leaves the new plan active in RAM as UNSAVED, without
claiming rollback. QUERY synchronizes durable state; SAVE retries persistence
of the current active bytes without repeating activation.

Boot restore runs once and independently revalidates saved bytes against this
firmware. Empty/rejected storage keeps control available. Read failures remain
errors. Restore never rewrites flash. Reapplying identical active revision/bytes
after boot does not restart devices.

## Wire version 1

Every frame has this 24-byte header followed by exactly `length` bytes. Integers
are unsigned little-endian; native C structures never cross the wire.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | ASCII `UTH1` |
| 4 | 1 | Version 1 |
| 5 | 1 | Command; response sets bit 0x80 |
| 6 | 2 | Reserved zero |
| 8 | 4 | Nonzero request/correlation ID |
| 12 | 4 | Requested revision, echoed in response |
| 16 | 4 | Payload length |
| 20 | 4 | CRC over bytes 0..19 followed by payload |

CRC-32/ISO-HDLC: reflected polynomial 0xEDB88320, initial 0xFFFFFFFF, final
complement; `123456789` gives 0xCBF43926. CRC is not authentication.

| Command | Value | Request revision / payload |
|---|---|---|
| APPLY | 1 | Nonzero / 1..8192 exact profile bytes |
| QUERY | 2 | Zero / empty |
| SAVE | 3 | Active revision / empty |
| RECOVER | 4 | Zero / empty |
| PROBE | 5 | Zero / empty |
| TELEMETRY | 6 | Zero / empty; [observation format](../services/TELEMETRY.md#wire-contract) |
| CAPABILITIES | 7 | Zero / empty; [capability format](../../../tools/configurator/README.md#read-only-capabilities) |
| DISCOVER | 8 | Expected active revision / 24-byte reviewed request; [format](../discovery/README.md) |

Controller/probe replies total 48 bytes: header plus six uint32 fields **status,
active revision, saved revision, active CRC, saved CRC, flags**. PROBE substitutes
`last_request_id` for status. Profile CRC covers exact bytes; even whitespace
changes require a new revision. Zero revision means absent/unknown as qualified
by flags. Observations have separate variable payloads.

| Status | Value | Meaning |
|---|---|---|
| OK | 0 | Query completed |
| SAVED | 1 | Active bytes durably saved |
| UNSAVED | 2 | Activated; save failed or is uncertain |
| REJECTED | 3 | Rejected/restored or invalid state |
| RECOVERY | 4 | Retained resources/uncertain state |
| STALE | 5 | Older request ID or revision |
| CONFLICT | 6 | Reused ID/revision with different content |
| STORAGE | 7 | Durable state could not be established |
| EMPTY | 8 | Resources released or no valid saved record |

Flags 1/2/4/8 mean received/validated/activated/saved; 16/32/64 mean durable-known,
recovery-required/active-known. They describe final request milestones, not
sensor health or intermediate replies.

## Transport and request identity

The parser scans magic and rejects bad headers/lengths/CRC before callbacks.
Its 2000 ms whole-frame deadline starts with the first consumed magic byte and
does not extend with progress. Empty polls expire it; backwards time also rejects
the frame. Unread hardware bytes have no arrival timestamp guarantee. Noise is
not acknowledged. A completed request remains retained until reset.

The pump processes at most 64 RX bytes, one request and bounded TX attempts
(at most 48 bytes each), retaining partial work. Whole-reply deadline is 2 s
starting on the next transmit step after controller work returns. Disconnect,
backpressure or invalid callback counts discard transport state, not an applied
profile or controller cache. The C3 SDK LL USB FIFO has one owner, no competing
driver/console logs and explicit short-packet flushes. Boot/panic noise may
interrupt framing; the client resynchronizes. No USB sleep support is claimed.

Mutation/controller IDs increase within a boot, without wrap. The last exact
request caches its reply, including failures: identical retry does no I/O;
changed bytes at the same ID conflict; older IDs are stale. QUERY consumes an
ID and replaces the cache. PROBE/observations consume no mutation ID or cache
entry; PROBE does no storage access. At UINT32_MAX reboot is needed for a new
controller request. Revisions also never wrap.

Only one host client owns USB. It probes, queries, then sends explicit mutations
with a 10 s overall deadline and finite serial timeouts. It never automatically
resends an ambiguous mutation. Reconnect and query active/saved revisions/CRCs
before choosing retry or SAVE. Reboot clears the request cache: durable checks
are not an exactly-once transaction log. The local link is unauthenticated.

## Storage record and flash layout

Two independently erasable slots each need at least 8224 bytes. The C3 partition
`uthome_cfg` (custom type/subtype `0x40/0`) occupies 0x110000..0x115FFF: 0x6000
bytes, two 12288-byte slots of three 4096-byte sectors. NVS, PHY and the 1 MiB
factory range stay separate. Check exact writable, unencrypted internal-flash
geometry before use; there is no fallback formatting or partition migration.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | ASCII `UTHC` |
| 4 | 4 | Record format 1 |
| 8 | 4 | Nonzero revision |
| 12 | 4 | Payload length, 1..8192 |
| 16 | 4 | Payload CRC |
| 20 | 4 | CRC of first 20 header bytes |
| 24 | 4 | Commit marker 0xC35A3CC3, written last |
| 28 | 4 | Reserved erased value 0xFFFFFFFF |
| 32 | length | Exact profile bytes, no added terminator |

Save synchronizes, erases only the inactive slot, writes and reads back header
and payload, then writes/synchronizes/verifies the commit marker. Identical
revision/content does not erase/write. Lower/conflicting revisions fail; equal
committed revisions in both slots conflict. Load checks bounds/CRCs/marker;
partial records can fall back to the other valid slot. Read/sync failure is an
error, not an empty record. A write may be durable despite an error reply;
QUERY/SAVE resolve uncertainty without repeating activation.

SDK partition operations are synchronous; there is no wear levelling. Flash
work can suspend kernel dispatch and delay workers/non-IRAM interrupts. Physical
power-loss, wear, missed UART/pulse events and combined-load timing remain
acceptance work. Install the correct partition table, preserve a board backup,
and never use blanket erase to fix a storage error.

## Verification and manual C review

Host suites cover malformed/duplicate/oversized JSON, allocator exhaustion and
reuse, real C/Python wire parity, callbacks/reentry, partial transport, stale and
conflicting requests, rollback, retained cleanup, boot revalidation and injected
storage interruption. Compiler warnings remain errors; build evidence is in
the [qualification index](../../qualification/README.md). Simulation and linked
type sizes do not certify electrical behavior, free RAM or stack high-water marks.

The following preserves the scope of earlier manual reviews; it does not claim
a new hardware sign-off. Original dated wording/counts are recoverable through
the [documentation history](../../../docs/README.md#historical-references).

### Transfer C review

Paired modules/prefixes; fixed-width arithmetic and unsigned shifts; bounds
before narrowing; explicit serialization, no raw C structures; stable borrowed
buffers with aliasing prohibited; reentry and bounded loops; initialized metadata;
checked backend/activation outcomes and preservation of old data on failure.

### Admission C review

Copied scalar settings without pointers; alignment and bounded allocation;
complete tree release; bounded recursion/loops; callback serialization/lifetime;
explicit status mapping; stopped-worker resource release and retained failures.
Vendor spelling stays inside existing exceptions.

### USB C review

Paired modules/typed callbacks, explicit widths and 64/48-byte conversion bounds;
signed counts checked before conversion; SDK-documented enum casts; private
static lifetime contexts; SDK LL MMIO only; bounded loops and checked cleanup.
Intentional ignored pump/restore errors are documented because control must
remain available. Existing C99/C11/GNU99 boundaries, formatter, deviations and
approval remain unchanged. No new ISR/task entry ABI or exception was added.
