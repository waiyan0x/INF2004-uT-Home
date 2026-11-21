# μT-Home

A configurable embedded platform for INF2004, group AAI13. The project aims
to let a user change how an ESP32-C3 communicates with peripherals at runtime,
without recompiling or reflashing the firmware for each change.

## Resume next session

The professor has approved **uT-Kernel on the ESP32-C3, MQTT for commands and
results, Home Assistant MQTT discovery for display, and a rule-based decision
program on the ESP**. This approval was recorded from the user's confirmation
on 13 November 2026. The earlier ESPHome-native-API display choice is superseded.
Keep and simplify this repository; a firmware rewrite is not the plan.

Work in this `Revised` checkout. Check Git status and the relevant area guide.
The maintained host compiler and client are now in
[tools/transaction](tools/transaction/README.md), with current examples in
[configs/transactions](configs/transactions). Markdown compiles directly to the
current `TJ` rules. The ESPHome/native API and old USB `TR` proof are retired.

The `TJ` v2 session/status contract is implemented and software-tested. The host
can query boot identity, generation, active rule and pending/completed command
state; commands and replies carry the boot identity. Wire messages remain 96
bytes, with device identifiers now limited to 15 characters. Old v1 transaction
JSON/packets must be replaced; the examples and compiler emit v2.

The [persistent host controller](tools/transaction/README.md#persistent-controller)
now owns IDs/revisions, rule selection/loading/execution, durable pending commands
and display expiry. Software tests use the compiled C owner and a real local
TLS MQTT broker, including lost replies and host/broker restarts without repeated
I/O. The command-line session needs no UI changes. ESP restarts and generation
changes are injected in C fixture tests. The live BMP388 MQTT check below now
also covers rule switching, host reopen and broker recovery on the board.
Generated verification is local-only under `build/controller/`.

**Static-memory compliance is the first priority.** The tested image fits its
fixed DRAM budget, but its TCP/IP and certificate verification paths still
use the heap. The [tribunal memory check](docs/reports/wifi-static-memory/README.md#tribunal-static-memory-check-20-november-2026)
contains the per-module table, board counters and remaining acceptance tests. The
[one-broker TCP settings](docs/reports/wifi-static-memory/README.md#one-broker-tcp-sizing)
reduced the measured lwIP peak from 3,496 to 1,884 bytes through reconnect
and renegotiation. The ordinary image passed eight BMP388/radar reads.
TCP connections, segments and outgoing TCP packets now use fixed slots.
The Wi-Fi station has eight fixed references for incoming frames. The TCP/IP
thread has eight fixed messages for those frames and eight for callbacks.
The compiled receive and dispatch test returned all 23 driver buffers exactly
once. Booting from saved idle, the board passed eight BMP388/radar reads
across a broker restart. The receive pool peaked at three of eight slots on
that boot.
The station now reserves fixed storage for DHCP client state and outgoing
messages, UDP endpoints, ARP requests and replies, and records for packets awaiting
address resolution. The USB request and reply share a buffer. Compiled DHCP
renewal and rebinding passed with the ordinary TCP packet slots full.
On the ESP32-C3, the active interface held a bound lease in the fixed client
and all eight BMP388/radar reads passed across a broker restart.
The TCP/IP scheduler now uses twelve fixed records. On this image, the board
completed all eight sensor reads across a broker restart. A snapshot found
five live timers, each in its reserved pool.
The station shares one full-size frame between ARP queue copies, Wi-Fi chain
copies, ICMP replies and outgoing IPv4 fragments. The UDP header has its own
slot. On the IPv4 packet image the ESP completed eight sensor reads across a
broker restart and answered two full-size pings. The debugger observed the
echo handler calling the fixed allocator. Linked-firmware replay covered
fragmentation, exhaustion and recovery with zero general heap calls in
those paths.
The MQTT connection now uses one fixed IPv4 broker address record.
The previous firmware requested 308 heap bytes per lookup. The new
ESP-TLS socket replay used zero heap calls for address lookup, including
failure and retry. The ESP completed eight BMP388 and LD2450 reads on one
boot across a broker restart. Two lookups reused and released the fixed
record.
The Wi-Fi station now holds its IPv4 all-systems multicast membership in a
fixed 16-byte record. In the linked firmware, starting and stopping the
station reused that record without calling the heap. One station membership
fits the demo. A second simultaneous membership returns a capacity error.
On the ESP, all eight BMP388 and radar reads matched their MQTT replies
across a broker restart. The station held the same fixed slot before and
after that restart.
The default Wi-Fi station constructor now uses fixed storage for all seven
records, including its registry entry. The compiled constructor, destruction
and reuse made zero general heap requests. All seven addresses matched on
the ESP before and after a TLS broker restart, with eight BMP388 and LD2450
results verified on one boot.
The SDK default event loop and its registration tree now have fixed storage.
The compiled firmware created all 27 records, replaced a handler and cleaned
up eight simultaneous deferred removals without using the general heap for
registration records. On the ESP, all 27 records kept their fixed addresses
across two broker restarts and a live switch to idle. Eight BMP388 and radar
reads passed on that boot. Two separate reboots into saved idle timed out
before the first read. The station's SDK event bodies now use two 48-byte
slots and eight dedicated handler-cleanup slots. The compiled firmware passed
the 208-byte overlap case with 25 application bytes left under the RAM cap.
On the ESP, all nine observed startup and Wi-Fi rejoin messages used fixed
storage. Authenticated MQTT recovered on the same boot.
[Fixed queued event bodies](docs/reports/wifi-static-memory/README.md#fixed-queued-event-bodies)
The Wi-Fi build reads the demo CA from flash and shares the existing USB
workspace for certificate parsing and verification. A temporary 65-byte
P-256 public key now fits in that workspace alongside the typed EC keypair.
On the compiled demo image, CA parsing, peer parsing and broker verification
each make zero heap requests when the certificate workspace is available.
The subsequent import of the broker public key into the handshake context
also makes zero heap requests with the workspace available. The imported
key remains usable after the workspace returns to USB. The preceding
fixed-name image passed broker restart and TLS renegotiation on the ESP.
Whole-handshake and Wi-Fi heap qualification remain open.
[Handshake public-key import](docs/reports/wifi-static-memory/README.md#handshake-public-key-import)
[Fixed peer certificate names](docs/reports/wifi-static-memory/README.md#fixed-peer-certificate-names)
[Fixed parser integers](docs/reports/wifi-static-memory/README.md#fixed-parser-integers)
[Fixed verifier point table](docs/reports/wifi-static-memory/README.md#fixed-verifier-point-table)
[Fixed PSA public-key copy and measurements](docs/reports/wifi-static-memory/README.md#fixed-psa-public-key-copy)
[Fixed EC keypair and phase measurements](docs/reports/wifi-static-memory/README.md#fixed-ec-keypair)
[Fixed modular-inverse buffer and measurements](docs/reports/wifi-static-memory/README.md#fixed-modular-inverse-buffer)
[Flash-backed trust anchor and remaining allocations](docs/reports/wifi-static-memory/README.md#flash-backed-demo-trust-anchor)
[Fixed integer buffers and compatibility tests](docs/reports/wifi-static-memory/README.md#fixed-integer-buffers-for-the-demo-broker)
[Certificate arithmetic measurements](docs/reports/wifi-static-memory/README.md#elliptic-curve-window-for-the-demo-broker)
[Fixed key table and certificate memory](docs/reports/wifi-static-memory/README.md#fixed-key-table-in-the-wi-fi-build)
[Fixed SDK event records](docs/reports/wifi-static-memory/README.md#fixed-sdk-event-records)
[Station interface storage and board results](docs/reports/wifi-static-memory/README.md#fixed-station-interface-and-identifiers)
[Default event-handler replacement](docs/reports/wifi-static-memory/README.md#default-event-handler-replacement)
[Wi-Fi startup and reconnect heap callers](docs/reports/wifi-static-memory/README.md#wi-fi-startup-and-broker-reconnect-heap-callers)
[Fixed station membership](docs/reports/wifi-static-memory/README.md#fixed-station-multicast-membership)
[Fixed broker endpoint](docs/reports/wifi-static-memory/README.md#fixed-numeric-broker-endpoint)
[Earlier resolver and multicast checks](docs/reports/wifi-static-memory/README.md#numeric-broker-lookup-and-multicast-membership)
[IPv4 packet checks](docs/reports/wifi-static-memory/README.md#fixed-icmp-and-outbound-ipv4-fragments)
[Frame copy checks](docs/reports/wifi-static-memory/README.md#fixed-arp-and-wi-fi-frame-copies)
Certificate verification and the Wi-Fi driver allocation inventory remain open.
[Timer memory checks](docs/reports/wifi-static-memory/README.md#fixed-tcpip-timeout-records)
[DHCP memory checks](docs/reports/wifi-static-memory/README.md#fixed-dhcp-client-and-messages)
[ARP and UDP memory checks](docs/reports/wifi-static-memory/README.md#fixed-arp-and-udp-storage)
[Wi-Fi receive and dispatch checks](docs/reports/wifi-static-memory/README.md#fixed-tcpip-message-handoff)
[Outgoing TCP packet checks](docs/reports/wifi-static-memory/README.md#fixed-outgoing-tcp-packets)

**Demo-broker TLS acceptance passed on the physical ESP32-C3.** On 18 November, the user
moved dedicated investigation of the intermittent MQTT delays to last priority.
Continue bounded allocation work, reviewed peripheral descriptions, physical
functional integration and delivery preparation first. Retain the delay evidence;
deferral does not waive the final reliability or hardware acceptance checks.
At the user's request on 17 November, the [ESP HTTPS service](firmware/src/network/README.md#esp-https-removal)
and its host setup commands are removed. Full saved-profile setup/recovery uses
USB; runtime rules/results use MQTT over verified TLS. The host Configuration Desk
and Home Assistant web interfaces are unchanged. The previous board image was
[temporarily tested on the board](tools/transaction/README.md#usb-mqtt-observation-board-trial);
the original full flash was restored and verified afterward.
MQTT now reserves its full-size TLS input/output buffers, retained broker
hostname, typed outer handshake state, temporary certificate head and retained
peer fingerprint; see the
[resource assessment](docs/reports/wifi-static-memory/README.md#current-network-resource-assessment).
TLS 1.2 key derivation reserves its 64-byte seed, 22-byte label and two
66-byte stores for the imported key and secret copy. After the key store
warms up, the compiled client derives the expected SHA-256 and SHA-384 outputs
with zero heap requests in the tested import and derivation path. The earlier dynamic
PSA key tables allocated 1,408 bytes at startup and
704 on first use.
The ESP32-C3 used at most one slot while joining Wi-Fi with the broker offline
and three during each verified MQTT TLS connection, including after a broker
restart. Both 96-byte status replies came from the same boot. One follow-up
status check after debugger detachment timed out. An offline 30-slot build
left 18 bytes in the SDK/kernel budget. In emulation it rejected a 31st key
and resumed TLS derivation after one slot was freed. In a separate laptop
trace, authenticated TLS renegotiation peaked at five slots whether the
client or server requested it. On the ESP, server-requested renegotiation
also peaked at five slots and returned to two. Three MQTT status replies
confirmed the same boot across the measurement. With one extra AES key held
through renegotiation on the laptop, a five-slot build exhausted its table
and a six-slot build completed the handshake. The six-slot ESP image leaves
978 bytes in the SDK/kernel budget and 6,006 bytes in total fixed DRAM. Its
RV32 exhaustion and reuse tests passed. On the normal profile, this six-slot
image also passed Wi-Fi startup, MQTT broker restart and server-requested TLS
renegotiation on one ESP boot. The remaining capacity check is a confirmed
Wi-Fi group-key renewal during TLS renegotiation. A ten-minute watch on the
Windows hotspot detected the initial key installation and zero renewals. Resume
this check when renewal timing is known or controllable. The USB configuration parser now lends its 4 KiB workspace to up to four peer
intermediates. In the six-slot ESP emulator, verification peaked at 3,834
requested heap bytes for the demo broker and 5,090 for the three-certificate
RSA chain. Both passed with a 5,990-byte payload ceiling. A seven-certificate
chain needed 6,978 bytes and exceeded that ceiling. The repository-default firmware also passed the
physical demo-broker check on the normal profile. Three authenticated status
replies identified one boot across initial connection, broker restart and
server-requested TLS renegotiation. The original full flash and profile were
restored and verified. General fixed-memory qualification and Wi-Fi key-renewal
overlap remain open.
[Demo-broker acceptance](docs/reports/wifi-static-memory/README.md#physical-demo-broker-acceptance)
[Certificate parser measurements](docs/reports/wifi-static-memory/README.md#broker-certificate-parser-heap)
[Shared-key capacity tests](docs/reports/wifi-static-memory/README.md#wi-fi-key-lifetime-during-tls-renegotiation)
[Fixed-table results](docs/reports/wifi-static-memory/README.md#offline-fixed-table-build)
The [certificate record work](docs/reports/wifi-static-memory/README.md#fixed-peer-certificate-records)
preserves the native validation and renegotiation lifecycle. The user approved
[disabling session tickets](docs/reports/wifi-static-memory/README.md#mqtt-session-tickets-disabled)
on 17 November: accepted tickets exceed available headroom, and the application
does not reuse saved sessions. Reconnects retain full verified TLS handshakes.
The [fixed handshake-state stores](docs/reports/wifi-static-memory/README.md#fixed-tls-handshake-state)
retain active records while renegotiation prepares replacements. Scoped setup
and both renegotiation entries preserve SDK ownership and deterministic exhaustion.
The simulated Runtime Desk and Home Assistant walkthrough is complete. The physical ESP has now returned BMP388 chip ID `50` over
TLS MQTT, and Home Assistant recorded the same result and byte counts.
[Physical chip-ID check](tools/transaction/README.md#physical-bmp388-chip-id-in-home-assistant)
The [physical Desk walkthrough](tools/configurator/README.md#physical-bmp388-walkthrough)
also passed. The Desk and HA recorded `50`, `24 87 6A` and `50` on one boot.
The [physical restart check](tools/configurator/README.md#physical-restart-recovery)
passed for the Desk, both MQTT brokers, HA Core and the ESP. Five explicit reads
produced five chip-ID replies. Recovery added no execution. The
[MQTT connect retry correction](firmware/src/network/README.md#mqtt-tls-connect-retry)
now passes software checks and a fresh target build. The approved cumulative
candidate answered initial and post-broker-restart status queries, then timed
out on the normal profile before any idle Apply. The updated
[read-only USB observation](tools/config_transfer/README.md#read-only-wi-fi-and-mqtt-status)
now exposes the existing MQTT state and failure/publication counters. Its host
checks, fresh target build and fixed-memory gate pass. The subsequent backed-up
diagnostic trial captured five minutes of normal traffic and a broker restart;
the later stall did not recur, and both original-image controls also passed.
The original full flash and profile were restored. A subsequent
[original-image trace](tools/transaction/README.md#original-image-quiet-interval-trace)
answered 399/399 status queries across USB polling and quiet intervals; the
earlier slow USB close did not recur. The subsequent
[diagnostic idle-transition trace](tools/transaction/README.md#diagnostic-idle-transition-trace)
passed 398/398 normal queries, then lost replies after Apply. Captures show
TCP setup failing before TLS, followed by Wi-Fi beacon loss; the board did not
reboot. A subsequent backed-up
[power-save comparison](tools/transaction/README.md#wi-fi-power-save-comparison)
passed normal and idle/quiet checks in both modes. Keeping the radio awake reduced
reply latency but did not establish a reliability fix. The original full flash,
profile and laptop network are restored; its final startup control answered 2/5
queries. Dedicated investigation of intermittent Wi-Fi/TCP failure is now deferred
to last priority. The timeout cause and runtime
resource compliance remain unresolved. A
[laptop-hotspot comparison](tools/transaction/README.md#laptop-hotspot-comparison)
then passed 390/390 normal queries and 11/11 after the idle quiet interval.
Its first recovery query preceded TCP reconnection and timed out; ten ready
replies followed. Two normal replies took 1.09/4.31 seconds with repeated TCP
data, so this run establishes neither a reliability fix nor a phone-hotspot
fault. The original full flash/profile and host settings are restored.

The latest [plan-change correction](firmware/src/network/README.md#mqtt-plan-changes)
keeps a healthy MQTT session at complete packet boundaries instead of forcing
a reconnect for every peripheral-plan change. Queued old-generation work is
discarded; partial packets and real outages still reconnect. The 158 affected
tests, full 334-file C gate and fresh target build pass with unchanged fixed
memory. The [backed-up laptop-hotspot trial](tools/transaction/README.md#plan-change-session-trial)
returned 334/338 normal-profile replies within eight seconds. Four replies reached
the broker after 11–33 seconds, so the idle Apply was skipped. A read-only repeat
passed 295/295 queries with the same boot identity; the earlier USB-polling
association did not recur. Captured TCP acknowledgements and return-data gaps
show transport delays, but do not identify their cause. The original full flash,
profile and host settings are restored and verified. The plan-change correction
still needs hardware acceptance. The CI fixture's obsolete HTTPS fields are
also removed; [all four jobs pass](https://github.com/waiyan0x/INF2004-uT-Home/actions/runs/44235575286)
for that correction.

The [Home Assistant display adapter](tools/transaction/README.md#home-assistant-display)
is implemented on the host. Add `--home-assistant` to the existing controller
session to publish six read-only entities from its copied state. Real-broker and
compiled-C software tests cover A/B/A switching, zero bytes, expiry, failures and
restarts without extra peripheral operations. The subsequent live trial received
six discovery definitions and a ready-state display over MQTT, but completed no
new rule load or execution: status replies became intermittent after applying the
idle plan. Home Assistant OS 18.4 / Core 2026.11.1 was installed and connected to
the broker, but its local VM repeatedly stalled. This was the earlier HA OS trial. See the [trial limits](tools/transaction/README.md#current-live-trial);
local-only evidence is under `build/homeassistant/` and
`build/homeassistant-live-20261115/`.

The [MQTT status investigation](tools/transaction/README.md#mqtt-status-investigation)
on 16 November reproduced idle-plan timeouts, but also observed MQTT timeouts
and ping loss on the normal profile. A plan change can inherit a 30-second
reconnect backoff; later delays/disconnections and one unattended reset during
Apply remain unexplained. Those trials established no firmware fix. Home Assistant VM
instability remains a separate unresolved issue. A later check kept the normal
profile fixed and captured repeated TCP packets with delayed acknowledgments.
The local broker and hotspot gateway responded promptly; moving the phone closer
did not clear ESP packet loss. The board's firmware and saved profile are unchanged.
An approved original/diagnostic/original firmware comparison then showed lower
ping latency with modem sleep disabled, but initial MQTT timeouts persisted in
both modes. The original full flash was restored and verified; no fix was kept.
TLS work was paused at that checkpoint while HA integration proceeded.

The separate [HA VM check](tools/transaction/README.md#home-assistant-vm-check)
passed six-sensor A/B/A, expiry, HA Core restart and host-reopen checks using
the compiled C owner with simulated buses and HA's authenticated state API.
The VM still stalled during shutdown with HPET enabled; it is not a stable host.
The approved [HA Container in WSL2](tools/transaction/README.md#home-assistant-container-in-wsl2)
is now running at `http://127.0.0.1:18123`. The isolated six-sensor check passed
with simulated buses, including HA and MQTT restarts without extra operations.
Both containers stopped cleanly, the WSL environment restarted successfully,
and all 30 API probes over five minutes passed in 3–25 ms. The user confirmed
that the dashboard loads normally. On 17 November, the user also verified all
six rendered sensors through chip-ID / raw-pressure / chip-ID switching,
result expiry and display disconnection using the simulated fixture. This is
separate from physical ESP acceptance. The old VM remains stopped with recovery
copies saved.

Use the local `build/homeassistant-wsl-20261116/start-ha.ps1` and `stop-ha.ps1`
helpers described in the guide. The start helper keeps this dedicated WSL
environment alive; systemd services alone did not keep it running. Login details,
test configuration, backups and verification are private under that ignored
folder. The fixture is stopped, so its six sensors are correctly unavailable.

**The [power-bank demo rehearsal](tools/configurator/README.md#power-bank-demo-rehearsal) passed.**
The Desk switched from BMP388 to LD2450 and back on one cold ESP boot while the
laptop USB cable was disconnected. All five results matched Home Assistant.
The [64-read switching test](tools/configurator/README.md#repeated-bmp388-and-radar-switching)
passed 16 BMP388/radar cycles on one boot. Both displays matched all reads.
Normal-mode allocations remain measurable. Complete the whole-system memory
assessment and final demo materials. Visible OLED control remains optional. See the
[completion plan](CONTRIBUTING.md#completion-plan). Dedicated delay
investigation and its guarded idle-session check are last priority. The
[timestamp recorder](tools/transaction/README.md#status-timestamp-capture) now
retains complete broker replies after a client timeout and brackets the matching
USB kernel clock. Its backed-up follow-up answered 393/393 queries, so it did not
reproduce the fault. The original flash/profile and host settings are restored.
The earlier four late replies still lack saved device timestamps. Use the
recorder when the delay recurs; another clean run alone will not localize it.
Do not treat USB polling, the phone hotspot or the plan change as an established
cause. Packet loss, the earlier unattended Apply reset and old VM instability
remain open.
The [completion plan](CONTRIBUTING.md#completion-plan) records the revised order.
The approved compact Runtime transactions tab is available in the existing
[Configuration Desk](tools/configurator/README.md#runtime-transactions): choose
a description, operation and address, then Load rule and Execute active rule.
The new HTTP path passed the simulated A/B/A, expiry and journal-reopen check
through MQTT and HA, with exactly three reads. Backend and desktop/mobile browser
checks passed. The user then completed the
[operator walkthrough](tools/configurator/README.md#simulated-operator-walkthrough):
preview versus active rule, three explicit A/B/A reads, expiry and disconnect,
with all six HA sensors unavailable afterward. The local HA container and simulated six-sensor visual check are ready. A
[repeatable demo](tools/transaction/README.md#repeatable-simulated-display-demo)
is available without the physical ESP. The subsequent physical chip-ID check used
the host controller directly. A subsequent physical test exercised the Desk controls.
The [power-save comparison](tools/transaction/README.md#normal-profile-packet-capture)
is complete, with the original image/profile restored and no root-cause claim.
The [physical SPI and UART check](tools/configurator/README.md#physical-oled-and-radar-transactions)
verified three OLED command writes and both 16- and 32-byte radar reads through the Desk and HA.
Use the [bench guide](HARDWARE_ACCEPTANCE.md#ssd1306-oled-over-spi) for OLED initialization and raw radar reception;
confirm current equipment and wiring before board work. Preserve the existing visual
style and review proposed UI adjustments before implementation. See the
[completion plan](CONTRIBUTING.md#completion-plan).

**Final resource acceptance remains open.** The latest target fits unchanged
fixed-memory budgets after removing HTTPS, but reachable TLS/TCP/IP heap
paths remain. The bounded board trial does not qualify runtime resource peaks.
Smaller TLS records can still break valid broker certificate chains, so working
limits stay unchanged. The
[resource assessment](docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
retains the implemented fixed-storage changes, build identities and unresolved
capacity/lifetime work. Its generated evidence remains under ignored `build/`;
none of the host display tests qualifies target memory, timing or stack use.

The [live network check](firmware/src/network/README.md#current-hardware-check)
on 15 November 2026 established working Wi-Fi with the full project firmware.
Correct the configured SSID's exact spelling and use the reviewed private
10 dBm startup/runtime setting for this board. The unchanged SDK example
connected at a 10 dBm startup limit and repeatedly failed at 20 dBm; the earlier
runtime-only override did not limit startup. The project now derives and checks
both settings. It connected after an ESP restart and automatically recovered
after a hotspot off/on cycle. Authenticated, certificate-verified HTTPS identity
and status reads succeeded.

The follow-up [live MQTT check](firmware/src/network/README.md#live-mqtt-check)
passed on that same firmware: Markdown-selected BMP388 reads changed from one
received byte to three and back, an invalid update was rejected by the ESP while
preserving the previous rule, and broker recovery retained the exact cached
completion. HTTPS reads succeeded alongside MQTT. SPI/UART runtime-rule tests,
physical Home Assistant/UI integration and resource acceptance remain open.
Those HTTPS read observations describe the earlier installed image; HTTPS is no
longer part of the maintained firmware.

The fixed project image is installed with private bench settings; active/saved
profile contents are now restored under revision 943, with the original CRC
3623560383. The final 16 November snapshot showed fresh BMP388/radar samples
and transfers from all three workers with no reported errors/timeouts. No new
firmware was flashed. The full original flash backup is retained. Private
settings, captures and image identities remain local-only under
`build/mqtt-idle-20261116/`, `build/mqtt-20261115/` and
`build/wifi-20261115/`; earlier observations are under
`build/bench-20261114/`. Confirm the current port, hotspot and broker address
before reusing any bench setup; the temporary broker is not a persistent service.

The existing configuration application and peripheral profiles remain for setup
and recovery. Runtime transactions require an explicitly idle peripheral plan.
Do not assume current equipment, port, wiring or installed firmware. Separate
host tests, target builds and physical acceptance.

<a id="platform-contract"></a>

## Target workflow

1. Prepare a few concise, reviewed Markdown peripheral descriptions
   based on datasheets. They contain the communication details needed for the
   supported examples. Automatically extracting PDFs is outside this scope.
2. On the host, the user selects a peripheral and an operation such as read or
   write. Host code turns the selected description and operation into a
   configuration or set of rules for the ESP.
3. The host sends the configuration over Wi-Fi using MQTT. The intended ESP
   decision program validates and applies the rules while running, then decides
   how the appropriate generic I²C, SPI or UART driver should communicate.
4. The generic driver performs the configured transaction. The ESP returns raw
   response bytes and status over MQTT. The host shows the result and publishes
   Home Assistant discovery and display messages; the user interprets the bytes.
5. Send different rules and demonstrate a changed transaction without
   rebuilding or reflashing the ESP. Show the active configuration and the
   actual decision and result so the runtime change is observable.

The decision component is a configurable rule-based program. The host obtains
explicit communication fields from the reviewed description; the ESP validates
and stores a rule, then selects the driver, outgoing bytes and receive length
when execution is requested. Loading a rule performs no peripheral I/O. No
trained weights, labelled dataset, automatic PDF interpretation or sensor-value
decoding is required. The bounded `TJ` contract, fixed wiring and supported
transfer shapes are linked from the transaction component guide.

```mermaid
flowchart LR
    D[Reviewed peripheral Markdown] --> H[Host selects operation and compiles rule]
    H -->|Update, then explicit execute| Q[MQTT request topic]
    Q --> O[ESP application owner and rule program]
    O --> G[Generic I2C / SPI / UART]
    G <--> P[Peripheral]
    O -->|Raw bytes and status| R[MQTT result topic]
    G -->|Transfer result| O
    R --> A[Host display adapter]
    A --> S[MQTT discovery and display state]
    S --> HA[Home Assistant]
```

The normal controller session uses one broker for commands, results and display.
The physical chip-ID trial used the bench broker for the ESP and the WSL broker
for HA display. The physical Desk walkthrough used the same two-broker setup.
MQTT discovery registers Home Assistant entities; state topics supply their
values. See the [Home Assistant MQTT documentation](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).

<a id="current-status"></a>

## Existing work and boundaries

Supported transfers are bounded; one firmware image does not imply support for
every peripheral protocol. Raw byte accuracy/decoding is not the demonstration
objective, but configured operations, actual status and limits must be honest.

Historical AI, framework comparisons and qualification archives were removed
from this checkout at the user's request. Current coding-policy approvals and
pinned dependencies remain. Fresh generated evidence belongs under ignored
`build/`; never commit private credentials, firmware backups or local references.

<a id="team-responsibilities"></a>
<a id="remaining-work"></a>
<a id="overall-delivery-plan"></a>

## Team and next steps

See the [team workload and integration checkpoints](CONTRIBUTING.md) for
ownership, handoffs and the planned end-to-end demonstration.

## Start here

### Repository layout

```text
firmware/       ESP32-C3 application, portable services, drivers and HAL ports
tools/          Transaction/configuration clients, wiring guide, builds and C tools
catalogue/      Reviewed boards, peripherals and connection presets
configs/        Runtime-rule examples, node profiles and build selections
schemas/        JSON contracts used to validate those inputs
datasheets/     Reviewed peripheral communication notes and source references
tests/          Software suites, board fixture and hardware test procedures
docs/           Cross-project guides, hardware notes and current Wi-Fi resource reports
build/          Local generated output and captured evidence; Git-ignored
```

Add portable firmware logic to the matching `firmware/src/` service, hardware
access to `firmware/hal/ports/`, and device-specific code to `firmware/drivers/`.
Keep host workflows in the relevant `tools/` package and tests in their existing
suite. Extend the area's main README when adding a capability. The
[documentation index](docs/README.md) and [hardware guide index](docs/hardware/README.md)
provide navigation without duplicating the implementation guides.

Keep generated builds, private settings and captured board backups separate
from maintained source. Original briefs, private professor reference code and
superseded Member 5 planning are stored outside this checkout in the sibling
`../Project References/` folder on this workstation. Its README records their
locations. These local reference files are not build inputs or shared project
requirements; use the current guides below for development.

### Area guides

| Purpose | Guide |
|---|---|
| Workload, interfaces and deliverables | [Team workload](CONTRIBUTING.md) |
| Set up or build the existing firmware | [Firmware](firmware/README.md) |
| Existing hardware wiring and acceptance procedures | [Hardware acceptance](HARDWARE_ACCEPTANCE.md) |
| Existing configuration application | [Configuration Desk](tools/configurator/README.md) |
| Existing network work and limitations | [Network](firmware/src/network/README.md) |
| Current rule-based decision work | [Transaction service](firmware/src/services/transaction/README.md) |
| Existing firmware contracts | [Shared contracts](firmware/CONTRACTS.md) |
| Software tests | [Tests](tests/README.md) |
| Required C policy | [BARR-C project standard](C_CODING_STANDARD.md), [agent rules](AGENTS.md) |

All new or changed project-owned C must follow BARR-C:2018 and the project
standard, including the pinned formatting/checker gate, manual review and
affected tests. Target changes require the applicable real-target build and new
evidence. The team is responsible for understanding and reviewing all code,
including code produced with AI assistance. Document testing and results in the
project report and raise blockers with the professor promptly.
