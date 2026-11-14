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
| ESP decision program | `txjob` validates/stores a rule, chooses execution and preserves it on a rejected update | `TJ` v2 session/status contract is implemented; durable host recovery remains |
| Generic operations | `txport` reaches actual I2C, SPI-write and raw UART HALs through `c3tx` | Physical acceptance, initialization prerequisites and selected timing extensions |
| Description compiler | `tools/transaction/description.py` compiles simple I2C register reads | SPI/UART operation descriptions; OLED source/description; SEN0390 review |
| MQTT | Bounded ESP transport and a one-shot TLS host client | Status query/boot binding implemented; long-running controller and durable restart recovery remain |
| UI | Existing Configuration Desk supports USB profiles and recovery | Runtime Markdown selection, rule loading/execution and result presentation |
| Home Assistant | Intended host-adapter boundary is documented | Adapter and live integration are not implemented |
| Verification | Cleanup baseline: 663 software tests, full 325-file C gate and a real target build passed | Real Wi-Fi/TLS/bus acceptance and whole-system resource measurements |

The current session build uses 275,930 of 278,528 fixed DRAM bytes, leaving
2,598 bytes in the total budget and 637 in the application group. MQTT still has
only 8 bytes within its module reservation. Generated verification is under
ignored `build/step1-verification/`. Fixed-linker fit is not free-heap or stack
acceptance; TLS/TCP/IP allocation remains a concrete integration blocker.

### 1. Close session and resource risks first

**Session contract implemented; resource assessment complete; allocation
remediation and board measurements remain open.** `TJ` v2 reuses the existing
HTTP/cfgmux boot identity and adds a read-only MQTT status query without growing
the 96-byte messages. It reports active rule, generation, busy/cleanup/readiness,
last completion, pending request and boot-relative times. Device identifiers are
now at most 15 characters; v1 wire packets and transaction JSON are rejected.

The [transaction contract](firmware/src/services/transaction/README.md) specifies
restarts, delayed replies, exact retries, ID exhaustion and generation changes.
The C engine and host codec preserve original cached completion times and retain
operation kind, byte counts and bus settings. One-shot CLI status and explicit
boot binding are available; automatic/durable controller recovery belongs to
Step 2. A reconnect never authorizes repeating a write with a new ID.

Verification: 472 target-facing tests (including 43 transaction/description
cases), 56 C-policy tests, 27 foundation tests and 19 runtime tests pass. The full
325-file C standard gate, manual review, fresh ESP32-C3 build, source-snapshot
verification and unchanged fixed-memory budgets pass. This totals 574 distinct
software tests in the current run; no live broker or equipment acceptance is
claimed.

The [current resource assessment](docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
identifies reachable heap allocation in ESP-TLS contexts, mbedTLS buffers,
lwIP netconn/packet pools and mailbox backing. The Wi-Fi-driver exception does
not exempt these application network paths. Static native tasks/queues alone
are insufficient. The configured TLS record capacities alone exceed current
headroom, so moving allocations into a fixed pool needs reclaimed storage and
a reviewed capacity/lifetime design. Keep the existing caps and TLS checks.

**Next priority:** implement bounded network allocation with deterministic
exhaustion and reconnect tests; then capture startup/reconnect/normal-operation
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

### 3. Complete the reviewed peripheral operations

Extend the versioned description contract only for supported transfer shapes:
I2C write/read, SPI write with reviewed D/C and CS, and UART raw receive/write.
Keep strict limits, source provenance and independent target validation. Do not
encode device-name cases in the generic drivers.

| Demonstration | Required work |
|---|---|
| BMP388 | Preserve chip-ID and three-byte raw reads; make initialization and possible zero data explicit |
| SPI OLED | Obtain/review the missing source description; document and support deterministic reset/initialization before claiming visible on/off behavior from a cold start |
| LD2450 | Add executable raw-chunk operations (for example 16/32 bytes); do not call them complete reports or require occupancy decoding; verify reliable UART reception on the actual setup |
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

Implement a small host-side adapter using the same controller results. Publish
stable discovery identities and JSON display state for status, raw hex, active
rule/protocol and counts. Keep configured bytes distinct from ESP-reported
counts; a received update acknowledgement is not a sensor reading.

Handle adapter/ESP availability, state expiry, broker reconnection and Home
Assistant birth/reload. Retain discovery definitions; keep raw result state and
commands non-retained. Re-announcing discovery must never execute a peripheral
operation. This uses the supported [MQTT discovery lifecycle](https://www.home-assistant.io/integrations/mqtt/#discovery-messages-and-availability)
and [sensor availability/expiry](https://www.home-assistant.io/integrations/sensor.mqtt/).

Done: actual Home Assistant displays real ESP results and becomes unavailable
when appropriate, including separate ESP, adapter, broker and HA restarts.

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
