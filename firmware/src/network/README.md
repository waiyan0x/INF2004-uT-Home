# Network services and ownership

The delivery path is uT-Kernel -> Wi-Fi/TLS MQTT -> raw transaction replies.
Home Assistant discovery/display belongs to the planned host adapter. ESPHome
firmware, the native API listener and on-device discovery formatting have been
removed. HTTP/USB configuration and existing read-only BLE diagnostics remain
because they support board setup, ownership checks and recovery.

## Module map

| Layer | Modules and responsibility |
|---|---|
| Portable resource compatibility | `netring`, `netwait`, `netpool`, `netevent`: bounded queues, deadlines, resource leases and event snapshots |
| Portable MQTT | `netmqtt`, `netbroker`, `netretry`: packet codec, one pending publication/request, connection state and capped retry |
| HTTP/BLE codecs | `nethttp`, `netread`, `netble`: bounded authenticated requests and copied observations |
| ESP transports | `c3wsta`, `c3mqtt`, `c3https`, `c3gsvc` under the HAL: SDK/socket boundaries |
| Application owner | `c3net` supplies metadata/copies; `c3tx` serializes runtime transactions with configuration |

The [HAL network guide](../../hal/ports/esp32c3/network/README.md) explains the
kernel/SDK compatibility boundary. The [transaction guide](../services/transaction/README.md)
owns `TJ` fields, driver limits and rule semantics. Networking does not drain
sensor queues, execute drivers in callbacks or mutate the active configuration
outside the existing controller.

## MQTT contract

| Topic under `uthome/v1/<node>/` | Payload | QoS / retained |
|---|---|---|
| `availability` | `online` / Last Will `offline` | 0 / yes |
| `transaction/request` | `TJ` v2, exactly 96 bytes | 0 / no |
| `transaction/result` | `TJ` v2, exactly 96 bytes | 0 / no |
| `telemetry/<device>` | Canonical sensor frame, 112 bytes | 0 / no |

Node identity is `uthome-` plus the lower-case 12-digit ESP MAC. The broker
connection verifies the CA chain and server name. Use an ACL that permits only
the designated controller to publish commands. There is no plaintext or
skip-verification fallback. Trustworthy clock/certificate-validity-time support
still needs deployment qualification with the pinned TLS setup.

A clean-session connection subscribes to the command topic and waits for SUBACK
before accepting work. Requests must be non-retained, non-DUP QoS 0. Only one
bounded command is queued; transaction replies take priority over sensor
telemetry. Slow transport preserves a partially written packet. Disconnect and
plan-generation changes drop queued transport work; uncertain writes are not
silently repeated. The transaction layer owns exact-ID retry semantics.

The board uses `UTHOME_MQTT_VALUE_BYTES=112` and
`UTHOME_MQTT_PACKET_BYTES=384`; tests exercise those actual bounds. Binary sensor
telemetry rotates through source slots at one-second offers. The `TJ` reply path
is separate and does not decode sensor units. No `homeassistant/...` configs or
sensor `state/...` JSON are published by this firmware.

## Private setup and target build

Use [the target build guide](../../../tools/target/README.md) and pinned SDK/kernel.
Select the normal configuration application with `--runtime-port
--configuration-app --static-net-queues --network-services wifi`, the standard
compatibility/resource patches, the task-observer patch, and
`--network-settings PRIVATE_SETTINGS.json`.

The settings parser accepts required `ssid`, `password`, `token`,
`certificate_file`, `key_file`; optional `port` (HTTPS default 8443),
`tx_power_quarter_dbm` and `mqtt`. MQTT fields are exactly `address` (numeric
IPv4), `server_name`, `port`, `user`, `password`, `ca_file`. PEM paths resolve
relative to the settings file. The removed `esphome_lab` key is rejected; update
private development settings explicitly instead of assuming port 6053 remains.

Settings, generated headers and firmware can contain credentials. Keep them
outside Git. Immutable-input hashes detect changes between staging and build;
this is development provisioning, not runtime credential persistence. Rules
continue to change at runtime without rebuilding that provisioned firmware.

## Resource and scheduling boundaries

Queues, event groups, task stacks and notification records use the existing
bounded compatibility pools. `c3task` owns startup allocation/sealing and checked
cleanup. Keep the pinned kernel/SDK and owned patches; do not copy vendor code
into new modules or weaken BARR-C exceptions. See the
[static-task design](../../../docs/reports/wifi-static-memory/README.md).

The current image policy is [transaction-budget.json](../../../tools/target/transaction-budget.json).
It retains the existing group, aggregate and task-pool caps. Removing display
code does not grant permission to raise any reservation. A target map check is
separate from target stack high-water, SDK heap, radio timing and combined-load
acceptance. No new worker slot is assumed.

The [current source/map assessment](../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
confirms reachable heap paths in ESP-TLS, mbedTLS, lwIP connection/packet pools
and mailbox backing. They are outside an automatic Wi-Fi-driver exemption.
Fixed native queue/task pools and a passing linker budget do not qualify these
allocations; bounded allocation remediation and runtime measurements remain open.
No-network and isolated radio fixtures do not qualify the combined application.

## USB Wi-Fi troubleshooting

Use `python -m tools.config_transfer --port CONFIRMED_PORT network` to read the
current station/broker diagnostics without starting a new connection or changing
configuration. Confirm the CLI's `--help` and current port before use. A previously
recorded COM port, hotspot or image is not proof of current equipment state.
The optional TX-power cap accepts 8-84 quarter-dBm units. It is a configurable
radio setting, not an established fix for every connection failure.

## HTTP and BLE observation contracts

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

## Authenticated configuration Apply and USB arbitration

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
no lock across flash/SDK work, and no new coding exceptions. Hardware timing,
TLS allocation and flash acceptance require physical checks on the selected image.

See the target-tools guide for explicit client commands. Store fresh build,
memory and test records under ignored `build/`.

## Verification and manual BARR-C review

Run the full pinned C standard gate and affected target tests with GCC warnings
as errors and assertions enabled. `test_network_esp` uses actual owned transport
sources with explicit SDK/TLS doubles and the board MQTT buffer limits;
`test_network_broker` checks framing/subscription/priority paths. These are
software checks, not a live radio, broker TLS or Home Assistant acceptance test.

Review for the cleanup: `c3https` retains its fixed 512-byte TLS reply buffer,
two-client bound and serialized configuration path; the plaintext/native branch
is gone. `c3mqtt` retains copied telemetry, broker deadlines and reply priority,
without discovery cursors/JSON state or their extra capture work. `c3net` still
checks transaction ownership before configuration and uses copied source data.
The removed `netread` transaction pointer was an internal display handoff, not a
wire field. Existing casts/SDK ABIs and fixed storage remain; no new allocator,
ISR mutation, task, language scope or deviation was introduced.

A fresh real-target build, unchanged memory limits and hardware acceptance are
required before claiming the cleaned image is qualified. C gate success does
not replace the manual type, lifetime, concurrency and hardware review.

## Remaining acceptance

- Live Wi-Fi disconnect/reconnect and verified broker TLS with real settings.
- A -> B -> rejected update -> B -> A over MQTT on one running image.
- Independent I2C/SPI/UART physical transactions and combined-load measurements.
- Host Home Assistant discovery/display adapter and actual UI observations.

Use the [team implementation plan](../../../CONTRIBUTING.md) to coordinate these
checks. Historical experiment records were removed from this checkout at the
user's request; they must not be presented as evidence for the cleaned build.
