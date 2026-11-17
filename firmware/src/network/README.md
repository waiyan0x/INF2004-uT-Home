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
resolves relative to the settings file. HTTPS `token`, `certificate_file`,
`key_file` and top-level `port` are rejected. Remove them in a new private settings
copy before rebuilding; retain the original inputs with the old image/backup.
The new image needs no device server certificate, private key or bearer token.
The superseded `esphome_lab` key also remains unsupported.

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
hostname scratch for the single MQTT connection. Its
source-scoped bindings retain the SDK lifecycle and reject exhaustion without
heap fallback. `c3tout` now also owns record buffers, the retained mbedTLS name
and typed outer handshake state, temporary peer-certificate head and peer digest.
Its derivation seed/label stores also replace those two PSA allocations while
keeping Wi-Fi's competing allocations separate by native task identity.
Session tickets are disabled under the approved
[MQTT policy](../../hal/ports/esp32c3/network/README.md#mqtt-session-ticket-policy);
the application already starts a fresh verified TLS connection on reconnect.
Inner certificate/key data, secret copies and remaining
TCP/IP storage are still open; see the [TLS storage contracts](../../hal/ports/esp32c3/network/README.md#fixed-tls-handshake-state).
Fixed native queue/task pools and a passing linker budget do not qualify these
allocations; bounded allocation remediation and runtime measurements remain open.
No-network and isolated radio fixtures do not qualify the combined application.

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
The [backed-up USB/TCP trial](../../../tools/transaction/README.md#usb-mqtt-observation-board-trial)
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

The following MQTT check extends this Wi-Fi result. Prolonged MQTT load,
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
This intermediate image is unflashed. The later cumulative candidate, including
this correction, completed an approved
[backed-up board comparison](../../../tools/transaction/README.md#backed-up-tls-board-comparison).
It answered initial and post-broker-restart queries, then lost normal-profile
replies before any idle Apply. That comparison does not isolate this correction
from the other changes. The original full flash was restored. Packet loss,
later status delays and the unattended Apply reset still require their own
observations. The subsequent USB/TCP trial did not reproduce the later stall
and does not isolate this correction either. The remaining
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

## ESP HTTPS removal

On 17 November 2026 the user replaced the earlier requirement to retain HTTPS.
The ESP listener, authenticated HTTP parser/replies, profile-upload mux and host
HTTPS commands are removed. The build selects TLS client-only mode and rejects
linked server entry points. `c3tls` and `c3tout` each reserve one MQTT session;
record limits and broker CA/name verification stay unchanged. Shared TCP/IP
queues/records still serve SDK traffic and have not been resized from a TLS count.

Use [USB configuration](../../../tools/config_transfer/README.md) for full-profile
Apply, export, restore and recovery, including explicit idle-plan preparation.
Runtime rules/results use MQTT. The host Configuration Desk and Home Assistant
web interfaces are unchanged. Older installed firmware may still expose HTTPS;
this source change does not install an image or establish a live timeout fix.

Verification passed: **509 target-facing tests**, the full pinned **333-file
C gate**, manual BARR-C review, a fresh warnings-as-errors ESP32-C3 build and
original-source snapshot verification. Twelve encrypted MQTT compatibility cases
matched their expected outcomes, including wrong-CA/name rejection. Compiled
RV32 probes passed all 12 connect cases, one-slot output exhaustion/erasure/reuse
with eight SDK setup and seven cleanup calls, and 12 input-retention/read cases.
The instruction probes double transport/heap/ROM/hash boundaries; input-read
cases inject post-decryption data. They do not qualify encrypted board behavior.

The new build is `C:/Users/kkak4/esp/uthome-mqtt-only-20261117/`.
Application SHA-256:
`bef502e50e545a6b3613a6c9fdb2c9c8359e251fcaf3ba8113b2639c3d500f62`.
Its `evidence.json` SHA-256 is
`be7a1c899ddd074762af0d8abdc393dff4db6ce68ba59ae25d35c11abb4861a6`.
Fixed DRAM is **251,346/278,528 bytes**, 18,032 below the preceding retry build.
Application storage is **122,228/147,456**, leaving 25,228 bytes. The two TLS
module reservations shrink and the HTTPS reservation is removed; all aggregate
caps stay unchanged. Private evidence/settings are under
`build/remove-https-20261117/`. No board, serial port or live network was accessed.
The image is **unflashed**; installation and physical acceptance remain deferred.

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

- Repeat broker/Wi-Fi checks on the final deployment network; the current proof
  uses one private hotspot and broker.
- MQTT reconnect, prolonged load/exhaustion and USB Apply during network traffic.
- Independent I2C/SPI/UART physical transactions and combined-load measurements.
- Live Home Assistant display with ESP results; the host adapter has passed
  local-broker/compiled-C and rendered simulated-HA tests; physical acceptance remains open.

Use the [team implementation plan](../../../CONTRIBUTING.md) to coordinate these
checks. Historical experiment records were removed from this checkout at the
user's request; they must not be presented as evidence for the cleaned build.
