# Team workload

This five-member split follows the professor-approved direction recorded in
[README.md](README.md): uT-Kernel, runtime rules, generic drivers and MQTT,
with Home Assistant MQTT discovery for display. Member 3 now owns that MQTT
display integration instead of the ESPHome native API. Each member owns an
area and its handoffs; these roles describe the existing contributions.

## 1. Three generic drivers — Member 1

- Own generic I²C, SPI and UART transaction support. Agree supported bus
  settings, send/receive lengths, timing and error behaviour with Member 4.
- Expose bounded operations to the ESP decision program without adding
  peripheral-specific interpretation to the generic drivers.
- Test each protocol independently, including unsupported requests and error
  cases, and document supported transfer shapes and limitations.
- Handoff: driver interfaces, bounds, status codes and reproducible tests.

## 2. Wi-Fi and MQTT — Member 2

- Own the host-to-ESP configuration and ESP-to-host result transport over
  Wi-Fi/MQTT, including connection and failure reporting.
- Agree message fields, identity, size limits and update/result semantics with
  Members 4 and 5. Ensure received configurations reach the appropriate owner
  and that raw bytes and status return to the host.
- Test connectivity, valid and rejected updates, disconnect/reconnect and
  message flow; state what has and has not been verified on hardware.
- Handoff: transport contract, setup instructions and integration tests.

## 3. Home Assistant MQTT display — Member 3

- Own a small host adapter that turns decoded transaction replies into MQTT
  discovery and display state. Reuse Member 2's transport and Member 4's codec;
  do not build another command controller or interpret sensor measurements.
- Show transaction status and raw hex, with device/rule identity, protocol,
  byte counts and observation time. Distinguish configured outgoing bytes from
  counts reported by the ESP, and update acknowledgements from executed reads.
- Handle adapter/ESP availability, expiry, Home Assistant restarts and failed
  transactions. A failed read must not display an earlier successful payload
  as new data. The host adapter must remain running for display updates.
- Reuse suitable discovery identity conventions and test cases in the host
  adapter. Native API and on-device discovery code have been retired.
- Handoff: display-topic contract, entity definitions, tests and real Home
  Assistant observations. This is an MQTT interface, not an ESPHome native API.

## 4. Rule-based decision program — Member 4 (us)

- Own the ESP-side decision program and its runtime-changeable rule or model
  contract. It selects how to use a generic driver based on received rules and
  an operation request; it does not need to interpret sensor values.
- Own the structured Markdown contract and compilation to the existing `TJ`
  transaction format. The approved program uses explicit rules; training,
  labelled data and weights are not required for this deliverable.
- Validate complete updates before applying them; define rejection behaviour
  and demonstrate that changed rules produce a changed decision without a
  rebuild or reflash. Agree serialization and ownership with Members 2 and 5.
- Test decisions, limits and switching independently, then integrate with the
  real drivers. The earlier timing-model work is historical, not this feature.
- Handoff: decision contract, implementation, test vectors and explanation of
  what can be changed at runtime.

## 5. Host configuration and system integration — Member 5

This role connects the four areas above into one host workflow and a reproducible
demonstration.

- Maintain reviewed Markdown examples with Members 1 and 4. Automatic PDF
  conversion is out of scope. Record sources and represent only operations
  and settings the implementation can support.
- Own the host flow for selecting a peripheral and operation, producing the
  agreed configuration, and displaying returned raw bytes and status. Coordinate
  with Member 2 on transport and Member 4 on the rule format.
- Own the controller session: explicit operation requests, rule revisions,
  request IDs, current plan generation, result correlation and reconnect handling.
  Agree fresh-start/restart handling with Members 2 and 4 rather than resetting
  IDs and assuming the board restarted.
- Maintain the task-based block diagram: functions, data flow, task triggers
  or schedules, and how to isolate a protocol such as UART during debugging.
- Coordinate end-to-end tests, demo instructions and the report with all
  members. Keep existing UI and visual style; discuss proposed UI changes before
  implementing them.
- Handoff: example profiles, host flow, diagram, integration evidence and demo.

## Completion plan

**Current priority (18 November): finish bounded TLS/network memory work.**
The user moved dedicated intermittent-MQTT-delay investigation to last priority.
Work in this order: remaining network allocation; reviewed SPI/UART descriptions
and prerequisites; physical Desk/ESP/HA functional integration and runtime resource
measurements; delivery documentation; then the deferred delay investigation and
final reliability/recovery closure. Prepare delivery materials early, but retain
unperformed/failed checks and limitations until final acceptance is complete.
Functional hardware checks still require current equipment, verified readiness
and applicable installation approval; do not relax those guards to bypass a delay.
The simulated Runtime Desk and HA operator walkthrough has passed. Continue
TLS work, including operator-dependent steps after the necessary input and
current equipment checks. The user lifted the earlier input deferral on 17 November. The
[connect retry correction](firmware/src/network/README.md#mqtt-tls-connect-retry)
has software and target-build verification. The approved
[cumulative board comparison](tools/transaction/README.md#backed-up-tls-board-comparison)
passed initial status and broker reconnection, then lost normal-profile replies
before any idle Apply. A later
[USB/TCP diagnostic trial](tools/transaction/README.md#usb-mqtt-observation-board-trial)
did not reproduce that stall; both original-image controls also passed.
The original image/profile were restored after each trial. Sustained MQTT
reliability and whole-system resource compliance remain final
acceptance requirements. The numbered sections below track the remaining areas.

The finished demonstration must let a user select reviewed peripheral Markdown
and an operation on the host, inspect the proposed communication, load the rule,
explicitly execute it and see raw bytes/status in the host UI and Home Assistant.
Changing rules, supported peripherals or protocols must use the same running
uT-Kernel firmware image.

The initial delivery uses one ESP32-C3, one controller, one active rule and one
outstanding operation. Peripherals can remain wired to their reviewed buses;
transactions execute sequentially with an idle peripheral plan. Existing
configuration profiles remain the setup/recovery path. Automatic PDF processing,
trained ML, sensor-unit decoding, arbitrary GPIO wiring, concurrent rule programs,
SPI receive and automatic rule persistence are outside this delivery. Network
credentials may be provisioned once for the demonstration; changing a peripheral
rule must not require another build.

### Reviewed baseline

| Area | Present implementation | Remaining gap |
|---|---|---|
| ESP decision program | `txjob` validates/stores a rule, chooses execution and preserves it on a rejected update; host journal recovers exact cached completions | Live combined-system acceptance |
| Generic operations | `txport` reaches actual I2C, SPI-write and raw UART HALs through `c3tx` | Physical acceptance, initialization prerequisites and selected timing extensions |
| Description compiler | `tools/transaction/description.py` compiles I2C register reads, SPI writes and raw UART chunks | OLED cold-start and sustained radar reception, SEN0390 review |
| MQTT | Live BMP388 rule switching, target rejection, cached results and broker recovery verified on the earlier image; HTTPS now removed | Wider secure-connection load and network allocation blocker |
| UI | Runtime transactions tab implements description selection, preview, load/execute and expiring results; simulated operator and physical BMP388 browser walkthroughs passed | Combined demo with additional peripherals |
| Home Assistant | WSL2 container displayed physical BMP388 results and recovered across five separate restarts | Prolonged combined-system load |
| Verification | 531 target-facing tests plus 59 affected tests rerun after SDK-binding corrections; full 337-file C gate, checked target image, SDK setup/free instruction probe and unchanged memory budgets | Current image on hardware, runtime SPI/UART, prolonged combined load and whole-system resource acceptance |

The [15 November hardware check](firmware/src/network/README.md#current-hardware-check)
resolved the observed Wi-Fi join failure with an explicit startup-power setting.
The same image rejoined after an ESP restart and a hotspot outage, while retaining
the original peripheral profile. This closes the hotspot connectivity blocker.
The follow-up [live MQTT check](firmware/src/network/README.md#live-mqtt-check)
also passed the BMP388 rule-switching and rejection sequence, host reopen and
broker recovery. The allocation requirement and complete three-protocol/UI/Home
Assistant demonstration remain open.

The [current assessment](docs/reports/wifi-static-memory/README.md#imported-tls-derivation-key)
gives the latest software image's measured fixed RAM and remaining budget.
Use those limits for the remaining TLS/TCP storage. The previous
[board trial](tools/transaction/README.md#plan-change-session-trial) records
the backed-up installation and restoration.
Fixed-linker fit is not free-heap, stack or timing acceptance.

### 1. Session contract and final resource acceptance

**Session contract implemented; resource assessment complete; allocation
remediation and board measurements remain open.** `TJ` v2 reuses the existing
application boot identity and adds a read-only MQTT status query without growing
the 96-byte messages. It reports active rule, generation, busy/cleanup/readiness,
last completion, pending request and boot-relative times. Device identifiers are
now at most 15 characters; v1 wire packets and transaction JSON are rejected.

The [transaction contract](firmware/src/services/transaction/README.md) specifies
restarts, delayed replies, exact retries, ID exhaustion and generation changes.
The C engine and host codec preserve original cached completion times and retain
operation kind, byte counts and bus settings. One-shot CLI status and explicit
boot binding are available; the persistent controller is described in Step 2.
A reconnect never authorizes repeating a write with a new ID.

Verification: 531 target-facing tests passed; all 59 affected TLS/qualification
tests passed again after the SDK-binding correction. The full 337-file C standard
gate, manual review, fresh ESP32-C3 build, source-snapshot verification and unchanged
group/total fixed-memory caps pass. The binding tests cover eight-entry profiles
with four enabled devices at late/interleaved positions, unchanged sample
identities, repeated driver replacement, five-worker rejection, rollback and
retained cleanup failures. TLS output tests cover three-buffer capacity, erasure,
setup failure, same-sized unrelated allocations and 1,000 reuse cycles. The
compiled SDK setup/free instruction probe also passes, with heap and hash calls
doubled; it performs no TLS handshake. All 47 qualification tests pass.
These are software/build results; this image
remains unflashed. Prior live network checks above apply to the previous image.

The [current resource assessment](docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
identifies reachable heap allocation in mbedTLS state/buffers,
lwIP packet/control-block pools and other SDK state. The mailbox backing path uses
the existing fixed queues, with exhaustion/reuse tests and linked-target proof;
that change adds no fixed RAM or native objects. The Wi-Fi-driver exception does
not exempt these application network paths. Static native tasks/queues alone
are insufficient. Compact configuration views reclaimed 24,600 fixed DRAM bytes
without changing the profile schema. The user authorized removal of the ESP HTTPS service on 17 November. The
listener, HTTP/profile-upload code and host commands are gone; full-profile setup
and recovery use USB. Runtime rules and results still use MQTT over verified TLS,
and the host Configuration Desk/Home Assistant web interfaces remain available.
This supersedes the earlier requirement to retain MQTT plus two HTTPS sessions.

`c3tls` and `c3tout` now reserve one MQTT context/error record and full-size
input/output buffers, the retained broker hostname and typed outer handshake
state, including overlapping active/negotiating transform and session records,
plus the temporary peer-certificate head and retained peer digest.
TLS derivation also reserves its seed, label, secret and temporary imported
key for the application's native task. The shared PSA table peaked at three
slots on the board during MQTT connection and reconnect. A 30-slot diagnostic
recovered from exhaustion in emulation. The laptop TLS client used five
slots during renegotiation. The ESP also peaked at five during a verified
server-requested renegotiation and returned to two occupied slots. In host
tests with one additional AES key held, five slots exhausted the table and
six completed renegotiation. The fixed-six ESP diagnostic also passed Wi-Fi
startup, broker restart and TLS renegotiation on its normal profile. Measure
the ESP during a confirmed overlapping Wi-Fi group-key renewal before
selecting a production capacity.
The [removal verification](firmware/src/network/README.md#esp-https-removal) and
[resource assessment](docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
record the fresh build and measured headroom. The user approved disabling unused
TLS session tickets on 17 November; reconnects still use full verified handshakes.
Inner certificate, key and TCP/IP state still need bounded storage.
Keep full record limits and certificate checks while
designing that storage. The pinned dynamic-buffer option remains rejected because
it loses partial headers across nonblocking retries. No cap increase or new
allocation exception is authorized by removing HTTPS.

**TLS work resumed on 17 November:** implement bounded network allocation with
deterministic exhaustion and reconnect tests, then
capture startup/reconnect/normal-operation
heap, native-object and stack evidence on the selected board. Do not mark
whole-system static memory complete from the linker table or host tests.

### 2. Build the persistent host controller

Extend the existing `tools/transaction/` package with one serialized controller.
It owns connection state, session recovery, request IDs, rule revisions, pending
operations, result correlation and display expiry. Reuse the current compiler,
codec and TLS transport. UI and Home Assistant consume its events; neither
becomes a second command owner.

Provide explicit select/preview, load and execute operations. Load must complete
successfully before execution becomes available. An unknown transport outcome
must remain unknown; reconnect must not repeat a write. Preserve command identity
and unresolved outcomes across host restart, or require a verified fresh session.

Make idle-plan preparation and restore an explicit setup workflow through the
existing configuration controller. Preserve/export the saved profile; do not
silently stop workers, rewrite saved settings or flash on connection. A reboot
can restore an old active profile, so readiness must be checked again.

Done: a user can run A -> B -> rejected update -> B -> A without typing request
IDs or generation numbers, including recovery from host/ESP/broker restarts.
First verify this with the compiled C fixture and a real local TLS broker.

**Software implementation complete:** `controller.py` owns the session and
`journal.py` commits exact packets before publication. `session.py` exposes
explicit JSON-line commands without manual counters or UI changes. Reconnect
queries only; explicit recovery retrieves a proven cached completion without
new I/O. An unseen command remains unknown and blocks another command. ESP boot
or generation changes invalidate the old active rule and require explicit load.
The current/last state is durable; it is not a complete transaction history.

Tests cover process termination, disk errors, cross-process ownership, stale
display data and boot/generation changes during recovery using the actual C
engine/owner. The loopback TLS broker test covers rule switching, host restart,
lost replies and broker restart. It also rejects incorrect hostname/login
credentials. Board Wi-Fi acceptance and the UI/HA integration remain separate.
See [usage and reproduction](tools/transaction/README.md#persistent-controller).

### 3. Complete the reviewed peripheral operations

Extend the versioned description contract only for supported transfer shapes:
I2C write/read, SPI write with reviewed D/C and CS, and UART raw receive/write.
Keep strict limits, source provenance and independent target validation. Do not
encode device-name cases in the generic drivers.

| Demonstration | Required work |
|---|---|
| BMP388 | Preserve chip-ID and three-byte raw reads; make initialization and possible zero data explicit |
| SPI OLED | Physical Desk and HA verified AE/AF writes. On idle the panel stayed blank after the normal setup bytes and all-pixels command. The normal profile restored content on the same boot. Isolate reset and SPI ownership before claiming visible idle control |
| LD2450 | 16- and 32-byte raw chunks passed on the physical ESP, Desk and HA. Sustained reception remains open |
| SEN0390, desired second I2C device | Resolve the source's register/timing ambiguity and EN handling; expose the HAL's existing read pause only through a reviewed rule extension; physically qualify before claiming device switching |

The HAL already has `read_pause_ms`; `txport` currently sets it to zero. This is
an integration gap, not a reason to write another I2C driver. Other protocols'
command/wait/read sequences need separate explicit support. The SHT4x and
unavailable ADXL345/VEML7700 hardware are additional scope unless chosen for the
demonstration. Never report every Markdown file as executable or hardware-tested.

Done: every operation offered by the final host workflow compiles to a supported
rule, has deterministic prerequisites and has appropriate reference tests.

### 4. Establish the actual Wi-Fi/MQTT deployment

Prepare reproducible private broker/CA/credential settings and Home Assistant
connection instructions. Confirm the board, wiring and installed image afresh;
preserve a full backup and obtain installation approval before a test flash.
Verify real TLS trust, server-name and certificate-time behavior with the pinned
SDK, plus AP/broker loss and reconnection. Do not disable TLS checks to get a pass.

Start this bench check early when equipment is available, using the current CLI
if the controller is not ready yet. It can expose network/resource blockers
before UI work. USB diagnostics remain a recovery tool; USB transactions and
host simulations do not count as live MQTT acceptance.

Done: the ESP receives a rule over actual Wi-Fi/MQTT, executes a real bus
transaction and returns correlated raw bytes/status on the same installed image.

### 5. Connect the user interface

The [simulated operator walkthrough](tools/configurator/README.md#simulated-operator-walkthrough)
verified preview versus active rule. The [physical BMP388 walkthrough](tools/configurator/README.md#physical-bmp388-walkthrough)
verified A/B/A reads, expiry and disconnect in the Desk and Home Assistant.

Propose a small Runtime Transactions view within the existing Configuration Desk
and review it with the user before UI implementation. Preserve the visual style.
Show the selected description/operation, connection/readiness, proposed protocol,
address/settings, TX bytes, RX length, active rule and last operation outcome.
Distinguish configuration accepted, execution complete, unavailable and unknown
outcome. Unsupported operations explain their missing capability and cannot run.

Use the controller from step 2 for all actions. Preserve the existing profile and
recovery views. Bounded display history should retain source/rule identity and
failure information without presenting stale bytes as a current measurement.

Done: the complete normal demonstration can be run through the UI without
editing JSON or entering internal request counters. Browser tests cover busy,
delayed response, reconnect, rejected update and stale-display behavior.

### 6. Add Home Assistant display

**Host implementation and MQTT-contract tests complete.** `homeassistant.py`
consumes copied controller snapshots, and `session --home-assistant` enables it.
It publishes stable discovery identities and state for status, raw hex, active
rule/protocol and counts. Configured bytes/counts remain separate from reported
results; an update acknowledgement never becomes a fresh reading. The existing
Configuration Desk and firmware are unchanged by this host adapter.

Tests cover adapter/ESP availability, expiry, broker reconnection, retained
discovery, non-retained raw state and HA birth/reload. Re-announcing discovery
adds no driver call. See the [setup, topics and limits](tools/transaction/README.md#home-assistant-display).

The [earlier live trial](tools/transaction/README.md#current-live-trial) received
discovery and ready-state messages, but completed no new rule execution. The
[WSL2 HA container](tools/transaction/README.md#home-assistant-container-in-wsl2)
subsequently passed simulated display and restart checks, including the rendered
operator walkthrough. The old VM instability remains a separate unresolved issue.
Physical combined acceptance remains open; the dedicated MQTT-delay investigation
is deferred to last priority, not marked fixed.

Physical BMP388 display, expiry and disconnect passed on 19 November.
The [physical restart check](tools/configurator/README.md#physical-restart-recovery)
also passed for the Desk, both MQTT brokers, HA Core and the ESP.

### 7. Complete integrated acceptance

On one selected image, demonstrate valid zero bytes, changes in command/read
length, protocol switching, invalid-update preservation and the chosen second
I2C device once qualified. Exercise stale/duplicate/out-of-order messages,
wrong sessions/generations, full buffers, oversized input, missing peripherals,
timeouts, cleanup failure and reconnects. Include a cold boot and sustained load.

Measure runtime allocation/creation calls, stack high-water marks and operation
latency under the combined load. The fixed linker budget is only one gate.
Re-run the pinned formatter/full C gate, manual BARR-C review, affected tests,
Linux sanitizer checks where required and fresh real-target builds for changes.
Do not weaken assertions, budgets or approved exceptions.

Done: each success/failure is tied to exact source/build identity and actual
wiring, with host simulation, physical observation and unperformed checks
separated. Follow [hardware acceptance](HARDWARE_ACCEPTANCE.md).

### 8. Package the final delivery

Provide one startup/setup path, private-settings template, reviewed descriptions,
reproducible builds/tests, a task/data-flow diagram, recovery instructions and a
short professor demonstration script. Show the unchanged firmware identity while
switching rules and retain an explicit limitations list.

The Week 13 submission requires the current memory-budget table, static-
allocation design, overflow/exhaustion demonstration and verification evidence.
Publish a small reviewed, sanitized final report in the maintained documentation;
keep credential-bearing firmware, raw captures and large outputs under ignored
`build/` or private storage. Retiring old history does not remove this final
submission requirement. Review the final diff and cloud checks before an
explicitly authorized commit/push/release.

Keep one main guide per maintained folder. Historical source/evidence is already
kept by the user outside this working checkout; it is not a current build input.
Current coding-policy approvals, pins, tests and useful setup/recovery tools stay.
The retired source holding copy under ignored `build/retired-cleanup-20261113/`
is local-only because automatic review blocked permanent broad deletion.

Every member follows [BARR-C:2018](C_CODING_STANDARD.md) and the checks in
[AGENTS.md](AGENTS.md): pinned formatter/full gate, manual review, warnings as
errors with assertions enabled, and applicable real-target builds. No budget,
policy exception or test assertion may be weakened to make cleanup pass.
Use [hardware acceptance](HARDWARE_ACCEPTANCE.md) for wiring/procedures. Host
simulation, target build and physical acceptance remain distinct. Raw measurement
accuracy is not the demonstration goal; communication, runtime change and honest
status are. Preserve Member 5's existing UI style and review UI changes first.
