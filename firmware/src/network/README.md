# Network services and ownership

The delivery path is uT-Kernel -> Wi-Fi/TLS MQTT -> raw transaction replies.
Home Assistant discovery/display belongs to the implemented
[host adapter](../../../tools/transaction/README.md#home-assistant-display). ESPHome
firmware, the native API listener and on-device discovery formatting have been
removed. USB configuration and existing read-only BLE diagnostics support
board setup, ownership checks and recovery. The ESP HTTPS service was removed
on 17 November 2026 at the user's request; MQTT TLS remains required.

## Module map

| Layer | Modules and responsibility |
|---|---|
| Portable resource compatibility | `netring`, `netwait`, `netpool`, `netevent`: bounded queues, deadlines, resource leases and event snapshots |
| Portable MQTT | `netmqtt`, `netbroker`, `netretry`: packet codec, one pending publication/request, connection state and capped retry |
| Copied observations | `netread.h` supplies shared metadata/capture types; `netble` encodes bounded MQTT/BLE sensor frames |
| ESP transports | `c3wsta`, `c3mqtt`, `c3gsvc` under the HAL: SDK/socket boundaries |
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
At a complete packet boundary, a plan change now preserves a healthy MQTT
session and subscription. A partial packet or unfinished connection still
requires a clean reconnect; see [plan changes](#mqtt-plan-changes).

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

The settings parser requires `ssid` and `password`; optional fields are
`tx_power_quarter_dbm` and `mqtt`. MQTT fields are exactly `address` (numeric
IPv4), `server_name`, `port`, `user`, `password`, `ca_file`. The broker CA path
resolves relative to the settings file and accepts up to 8,191 PEM bytes.
The provisioner embeds a single standard PEM certificate as binary DER. PEM
bundles and other previously accepted input layouts go to the SDK unchanged.
The build manifest still identifies the original PEM file. HTTPS `token`, `certificate_file`,
`key_file` and top-level `port` are rejected. Remove them in a new private settings
copy before rebuilding; retain the original inputs with the old image/backup.
The new image needs no device server certificate, private key or bearer token.
The superseded `esphome_lab` key also remains unsupported.

Settings, generated headers and firmware can contain credentials. Keep them
outside Git. Immutable-input hashes detect changes between staging and build;
this is development provisioning, not runtime credential persistence. Rules
continue to change at runtime without rebuilding that provisioned firmware.

## MQTT plan changes

`netbroker` now keeps a READY session when Wi-Fi is available and neither a
write nor a received MQTT packet is in progress. It discards the old outbound
mailbox and copied request, preserves diagnostic counters and adopts the new
generation. Existing keepalive timing continues. Partially written TLS data,
partial MQTT input, incomplete handshakes and actual outages retain the clean
reconnect path. Bytes already handed to TCP cannot be recalled; transaction
boot/generation checks still reject stale commands. No command is replayed.

The network regressions cover preserved subscriptions, old queued work, fresh
commands, keepalive success/timeout, partial input/output, Wi-Fi loss and
failure backoff through the actual ESP adapter with SDK doubles. The current
[physical summary](../../../tools/configurator/README.md#physical-verification)
records completed Desk/MQTT/Home Assistant trials. Long-duration radio
reliability and workloads outside the qualified scope remain unestablished.

Manual BARR-C review: the change uses C99, the matching header and a private
helper with a prototype. Boolean conditions, constant-left comparisons,
braces and initialized fixed-width locals follow the project rules. The
single application owner serializes invalidation with I/O; there is no new
ISR access, cast, allocation, loop or public ABI. Mailbox replacement uses its
actual type and retains the replacement counter; bounded clearing uses the
destination object's size. Failure and partial-packet paths retain existing
ownership and deadlines. The helper keeps the new branch out of the already
long broker step. No dependency, formatter, exception or resource cap changed.

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

The [current resource assessment](../../../docs/reports/wifi-static-memory/README.md)
records the measured single-broker workload under its specific Wi-Fi exemption.
The fixed mailbox, TCP/IP and TLS stores do not authorize unrelated SDK heap
clients. Session tickets are disabled under the
[MQTT policy](../../hal/ports/esp32c3/network/README.md#mqtt-session-ticket-policy).
Arbitrary certificate chains and forced concurrent USB parsing/TLS verification
are outside qualification; the latter can reach the general allocator.

## USB Wi-Fi troubleshooting

Use `python -m tools.config_transfer --port CONFIRMED_PORT network` to read the
current station/broker diagnostics without starting a new connection or changing
configuration. Confirm the CLI's `--help` and current port before use. A previously
recorded COM port, hotspot or image is not proof of current equipment state.
The version-2 observation includes the existing MQTT state and three counters,
allowing USB observations to be correlated with MQTT replies and TCP captures.
Version-1 firmware supplies the station fields and leaves MQTT fields absent.
See the [wire contract](../config/README.md#wire-version-1) and
[host command](../../../tools/config_transfer/README.md#read-only-wi-fi-and-mqtt-status).
The [backed-up USB/TCP trial](../../../tools/transaction/README.md#prepare-a-live-session)
verified these fields during normal traffic and broker recovery. The later
stall did not recur; the original image/profile were restored and the timeout
cause remains unresolved.
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

### Physical verification

The qualified single-broker workload has passed authenticated MQTT, broker
restart, TLS renewal, BMP388/LD2450 reads, Desk/HA display, expiry and disconnect.
See the [physical summary](../../../tools/configurator/README.md#physical-verification)
and [AAI13 scope](../../../docs/reports/wifi-static-memory/README.md).
Use fresh readiness/status on the actual deployment network. An earlier pass
does not establish indefinite reliability or qualify another certificate profile.

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

The [compiled connect probe](../../../tools/target/README.md#tls-connect-retry-probe)
executes actual adapter/SDK instructions with socket readiness and TLS boundaries
doubled. Regression cases cover repeated timeouts, later readiness and reopening.
Physical acceptance is summarized above; a diagnostic probe alone does not
establish radio reliability.

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

## ESP HTTPS removal

The ESP application provides MQTT/TLS commands/results. Profile configuration,
persistence and recovery use USB; the localhost Configuration Desk remains the
host UI. The board does not expose an HTTPS configuration server. MQTT TLS
verification and bounded single-session storage remain enabled.

### HTTPS removal C review

The remaining application hooks use C99 and matching updated public declarations
and callers. Removing `cfgmux` also removes its borrowed-upload state and busy
check; USB remains gated by `c3tx_busy` while asynchronous bus work retains pins.
`cfgbind_discovery_ready` still rejects transactions when plan workers own buses.
All calls remain on the serialized application owner; callback metadata is copied
and no ISR accesses the changed state. The shared `netread.h` types retain only
MQTT/BLE fields; wire formats and the USB controller/store are unchanged.

The one-slot TLS stores retain typed/aligned storage, initialization, erasure,
bounded lookup and deterministic exhaustion with no heap fallback for those
leases. SDK cleanup finishes before reuse, including reconnect/failure paths.
The host TLS fixture now owns only a verified client and no server key; its
borrowed path/name and copied ciphertext lifetime rules are unchanged. Removed
arguments and fields introduce no casts, tasks, locks, allocations, hardware
register operations, language extensions or new BARR-C deviations. Dependencies,
formatter and aggregate memory caps are unchanged.

## MQTT and BLE observation contracts

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

## Verification

Run the full pinned C standard gate and affected target tests with GCC warnings
as errors and assertions enabled. `test_network_esp` exercises actual MQTT,
Wi-Fi and BLE wrappers using explicit SDK doubles; `test_network_broker` checks
framing/subscription/priority paths. USB controller/pump/store suites retain
Apply, restore, recovery and interrupted-transfer coverage. These checks do not
qualify live radio, timing, heap use or physical ESP/Home Assistant integration.

## Remaining acceptance

The BMP388/LD2450 single-broker workflow and scoped static-memory assessment are
complete for their measured configurations. Recheck the final deployment network,
installed image and readiness. Remaining limits include indefinite reliability,
arbitrary certificate chains, forced USB/TLS overlap, optional BLE/coexistence,
visible OLED control after idle transition, and unmeasured electrical/cache-off
behavior. New workloads require new measurements; use the
[hardware procedure](../../../HARDWARE_ACCEPTANCE.md).

## Queue bookkeeping storage

Only the eight queue slots own ring descriptors. All 16 semaphore and 32 mutex slots keep their original limits and identities. Each queue operation selects its ring by the same slot index while the lease is held. Interrupt exclusion, payload partitions, wait deadlines and startup object counts are unchanged. On RV32 this removes 48 unused 24-byte descriptors.
