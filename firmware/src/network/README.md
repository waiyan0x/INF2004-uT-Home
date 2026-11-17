# Network services and ownership

The delivery path is uT-Kernel -> Wi-Fi/TLS MQTT -> raw transaction replies.
Home Assistant discovery/display belongs to the implemented
[host adapter](../../../tools/transaction/README.md#home-assistant-display). ESPHome
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
confirms reachable heap paths in mbedTLS, lwIP connection/packet pools
and other SDK state. They are outside an automatic Wi-Fi-driver exemption.
The fixed mailbox adapter now removes the separate `sys_mbox_new` backing
allocation in Wi-Fi service builds; it does not remove packet/connection/TLS
allocations. `c3tls` now bounds ESP-TLS contexts, error trackers and temporary
hostname copies for the existing one MQTT plus two HTTPS connections. Its
source-scoped bindings retain the SDK lifecycle and reject exhaustion without
heap fallback. Encryption buffers, certificates/keys and retained mbedTLS names
are still separate open paths; see the [TLS record contract](../../hal/ports/esp32c3/network/README.md#fixed-esp-tls-records).
Fixed native queue/task pools and a passing linker budget do not qualify these
allocations; bounded allocation remediation and runtime measurements remain open.
No-network and isolated radio fixtures do not qualify the combined application.

## USB Wi-Fi troubleshooting

Use `python -m tools.config_transfer --port CONFIRMED_PORT network` to read the
current station/broker diagnostics without starting a new connection or changing
configuration. Confirm the CLI's `--help` and current port before use. A previously
recorded COM port, hotspot or image is not proof of current equipment state.
The optional `tx_power_quarter_dbm` accepts 8-84 quarter-dBm units and applies
before each connection attempt. The build also derives the radio's startup
limit from it: divide by four, round down, then clamp to the pinned SDK's
10-20 dBm range. Thus `40` requests 10 dBm at startup and at connection time;
values below `40` can only lower the later runtime setting. Omitting the field
preserves the SDK's 20 dBm startup default and makes no runtime override.
The generated `network-power.defaults` and final `sdkconfig` are verified
against the private settings; changing the setting requires a fresh build.
This is a board-specific troubleshooting option, not a universal connection fix
or a measurement of actual radiated power.

### Current hardware check

On 15 November 2026, the same ESP32-C3 and wiring connected to a 2.4 GHz WPA2
Android hotspot. The local SSID capitalization was corrected first; the laptop
also verified the supplied credentials. With those same credentials, the
**unchanged pinned ESP-IDF station example** repeatedly failed authentication
at a 20 dBm startup limit, connected at 10 dBm, then failed again in a repeated
20 dBm control. The 10 dBm setting worked with both fresh and saved calibration.
These are diagnostic comparisons, not uT-Kernel or BARR-C qualification.
The electrical/RF cause was not measured; the lower startup limit resolves the
observed connection failure on this board. An earlier runtime-only cap did not
affect radio startup.

The full project was then built from `433b29e` plus the startup-power change,
using `tx_power_quarter_dbm: 40`. Its checked image SHA-256 is
`19570aaf606a8b1f89b5679d63b7557a6402fd18eeefd61d5f8447e338f711fa`.
All 519 target-facing tests, the full 329-file C standard gate, real-target
compiler/profile checks and source-snapshot verification passed. Fixed DRAM
remains 260,386/278,528 bytes; no memory limit or SDK/kernel pin changed.

Physical observations on that same project image:

- Wi-Fi stayed online throughout a 45-second observation with no SDK error.
- After an ESP restart, it was online by the first five-second follow-up.
- A user-controlled hotspot off/on cycle produced an observed loss of IP,
  bounded waiting/retry states and automatic recovery without reflashing or
  restarting the ESP. The reported disconnect reason retains the last failure;
  current `has_ip` and retry state identify recovery.
- Authenticated, certificate/name-verified HTTPS `GET /v1/identity` and
  `GET /v1/status` both returned HTTP 200. These were read-only requests.
- Active and saved profile 931 retained CRC 3623560383. BMP388/radar samples
  were fresh, and all three workers reported transfers without timeouts or
  bus errors. OLED appearance was not observed.

The original 4 MB flash was backed up and independently verified, restored
before installing the checked project image, then the installed flash regions
were verified. **The board now runs the fixed project image with private bench
settings**, retaining the original peripheral profile. The laptop's original
Wi-Fi was restored and its temporary test profile removed; no firewall rule
changed. No MQTT broker was started during this Wi-Fi check.

The following MQTT check extends this Wi-Fi result. HTTPS configuration writes,
wider secure-connection load and network heap/stack acceptance remain open.
Allocation counters are not free-heap/peak measurements, and unavailable worker
stack measurements remain unmeasured.

Fresh local-only evidence is under `build/wifi-20261115/`: the diagnostic
manifests/results, `project-installed.json`, `project-memory.json`,
`project-running.json`, `project-after-restart.json`,
`project-hotspot-cycle.json` and `https-roundtrip.json`. The isolated target
build is `C:/Users/kkak4/esp/uthome-wifi-fix-20261115`; earlier bench observations
remain under `build/bench-20261114/`. Settings, certificates, images and raw logs
must stay outside Git. USB Apply also saves the idle profile; follow the
[explicit restoration guidance](../../../tools/config_transfer/README.md)
instead of expecting a reboot to undo it.

### Live MQTT check

On 15 November 2026, the installed application was independently verified against
the same image SHA-256 above. No firmware source change or reflash was required.
The host used the private authenticated TLS broker, reviewed BMP388 Markdown,
the maintained MQTT client/controller and actual I2C0 SDA0/SCL1 at address 0x77.
An explicitly applied idle plan released the sensor workers before transactions.

- The persistent controller loaded/executed A (`00`, RX 1 -> `50`), B (`04`,
  RX 3 -> `05 6E 6B`), then A again within the same ESP boot and plan generation.
  Update acknowledgements reported no bus transfer; executions reported TX 1
  and the configured RX count. These are raw bytes, not pressure accuracy evidence.
- A host-invalid preview was rejected before publication. After that controller
  closed, a single diagnostic owner separately sent RX length 33 to the ESP.
  The ESP returned `invalid`, retained B's revision, and successfully executed B
  and then A. This distinguishes target rejection from host-only validation.
- Closing/reopening the host journal and stopping/restarting the broker preserved
  the last command identity and completion time. An explicit identical request
  retrieved the same cached reply. No new command was automatically executed.
  Two status queries timed out during broker recovery; readiness must be observed,
  not inferred from Wi-Fi alone. Initial broker startup also had a delayed join;
  its cause was not isolated, and no firewall change was required.
- Certificate/name-verified TLS 1.2 HTTPS status reads returned HTTP 200 both with
  MQTT ready and during the broker outage. This verifies one MQTT connection plus
  a transient HTTPS read, not the maximum two-HTTPS-client load or HTTPS writes.

The exact original profile bytes were restored and saved as revision 933 with
CRC 3623560383, then verified after restart. The new boot had no test rule or
command history; its active sensor plan correctly reported transactions not
ready. BMP388/radar data returned fresh, and all three workers reported no bus
errors/timeouts. OLED appearance was not observed. The 30-second normal-operation
window counted 8,429 allocations and 8,423 frees, requesting 549,580 cumulative
bytes across the audit's sources. These are not resident/peak heap figures or
allocation attribution; whole-system static memory remains unqualified.

Local-only evidence is under `build/mqtt-20261115/`: `controller-demo.jsonl`,
`target-rejection.jsonl`, `broker-recovery.json`, `https-*.json`,
`image-verify-restart.log`, `profile-restored.json`, `restored-running.json` and
`final-mqtt-status.json`. The broker was stopped after testing. Keep credentials,
packet journals and captures outside Git. Follow the
[live-session procedure](../../../tools/transaction/README.md#prepare-a-live-session)
for the next run; the diagnostic owner handoff is not a second concurrent client.

The later [display-adapter trial](../../../tools/transaction/README.md#current-live-trial)
received discovery/ready-state messages but encountered MQTT status timeouts
after applying the idle plan. A physical reconnect restored five successful
status queries on the normal profile; the subsequent idle-plan attempt timed
out again. This remains an open transition/reliability issue, not a replacement
for the earlier successful run above. That trial flashed no new image and ended
with the original profile restored as revision 941, CRC 3623560383.

The [current status investigation](../../../tools/transaction/README.md#mqtt-status-investigation)
also observed failures on the normal plan, an unattended Apply reset and a
30-second reconnect-backoff case. Later packet delays/disconnections remain
unisolated; no firmware fix is claimed. A subsequent normal-profile capture
showed repeated TCP data and delayed acknowledgments, including a 6.791-second
wait before acknowledgment of one status request. Local broker/gateway controls
were responsive; moving the hotspot phone closer did not clear ESP packet loss.
These observations do not locate the fault. The board remains on profile 943
with the same original CRC and unchanged firmware. See that guide for the
separate live/software evidence and the completed power-save comparison. That
approved comparison reduced ping latency with modem sleep disabled but retained
initial MQTT timeouts in both modes. The full original flash and saved profile
were restored and verified. No production change was kept from that comparison.
TLS work resumed on 17 November after the simulated HA/Desk walkthrough.

### MQTT TLS connect retry

The pinned SDK initializes its `select` read/write sets when TCP connection
starts. A readiness timeout clears both sets, but subsequent calls to
`esp_tls_low_level_conn` reuse them. With the configured one-millisecond poll,
one timeout can leave later calls watching no socket until the broker's
30-second connection deadline expires. This provides a concrete software
mechanism for a TCP connection that never starts its TLS handshake.

The existing `c3mqtt` adapter now reconstructs those sets from the SDK-owned
socket before each retry in `ESP_TLS_CONNECTING`. The SDK continues to own
socket creation, error handling, TLS setup and certificate verification.
The correction uses the pinned private context layout and the existing
serialized application owner. It adds zero mutable storage.

On 17 November, 122 affected network/TLS/build-qualification tests passed.
The final adapter fixture rerun passed all 38 tests, including three timed-out
polls followed by connection and another two timed-out polls after reopening.
The [compiled connect probe](../../../tools/target/README.md#tls-connect-retry-probe)
also passed 12 cases on each of the original and corrected RV32 images. The
original adapter stalls after a timeout, while the corrected adapter reaches
the handshake when the socket becomes readable or writable. The probe executes
the actual adapter and SDK connect instructions with socket readiness and TLS
handshake boundaries doubled.

The full pinned 340-file C gate, manual review, warnings-as-errors ESP32-C3
build, original-source snapshot verification and every fixed-memory budget
passed. Total DRAM is 269,378/278,528 bytes, with application storage at
140,255/147,456. Every group has the same usage and cap as the preceding TLS
output-buffer build.

The fresh build is `C:/Users/kkak4/esp/uthome-tls-retry-20261117/`.
Its application image SHA-256 is
`4d01003367559ad591ae0cef4f103679a4b862902af88345bc2c83c079dfb05f`.
Its `evidence.json` SHA-256 is
`446751c37d77e0efd0bc2989f4b6f0984f7fab10e324e9c12c475d3224653462`.
Private reports and logs are under `build/tls-retry-20261117/`.
The image is unflashed. A backed-up physical comparison is deferred pending
installation approval. The earlier packet loss, later status delays and
unattended Apply reset still require their own observations. The remaining
TLS/TCP/IP storage work is tracked in the
[resource assessment](../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment).

#### Connect retry C review

The changed adapter uses C99 and the existing matching header/API. The new
branch reads the pinned SDK's connection state and updates its existing
`fd_set` objects only while connecting. The socket was created by the SDK
before entering that state and remains owned until SDK destruction. `FD_ZERO`
and `FD_SET` retain the SDK's declared types and descriptor bounds. There are
no added casts, allocations, loops, tasks, interrupts or native objects.
The application owner serializes state access and cleanup. Initial connection,
handshake retries, error states and certificate settings retain their existing
control flow. The radio component now exposes the required private SDK header
to Wi-Fi builds, including builds with static queues disabled. No new BARR-C
deviation is needed.

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

- Repeat broker/Wi-Fi checks on the final deployment network; the current proof
  uses one private hotspot and broker.
- HTTPS configuration writes, two simultaneous HTTPS clients alongside MQTT,
  and prolonged load/exhaustion tests.
- Independent I2C/SPI/UART physical transactions and combined-load measurements.
- Live Home Assistant display with ESP results; the host adapter has passed
  local-broker/compiled-C tests, but actual Home Assistant UI observations remain open.

Use the [team implementation plan](../../../CONTRIBUTING.md) to coordinate these
checks. Historical experiment records were removed from this checkout at the
user's request; they must not be presented as evidence for the cleaned build.
