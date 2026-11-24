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

## Delivery scope

The submitted workflow is implemented: reviewed Markdown is compiled on the host,
loaded as a bounded TJ v2 rule, explicitly executed on the ESP32-C3, and displayed
in the Configuration Desk and Home Assistant. The same firmware switches between
BMP388 I2C reads and LD2450 UART reception. The generic layer also supports SPI
writes and raw UART transmission; visible OLED behavior after entering the idle
plan is not qualified.

One controller owns one journal per node. One rule is active and at most one
command is outstanding. Transactions require an idle peripheral plan. USB profile
configuration, saved-profile restoration and diagnostics remain available.
Automatic PDF conversion, trained ML, sensor-unit decoding, arbitrary wiring,
concurrent rule programs, SPI receive and automatic rule persistence are outside
the delivery.

The [presenter guide](tools/configurator/README.md#tribunal-2-presenter-guide)
covers setup, same-boot switching, expiry and recovery. The
[physical verification summary](tools/configurator/README.md#physical-verification)
distinguishes measured behavior from remaining hardware limits. The
[AAI13 assessment](docs/reports/wifi-static-memory/README.md#week-13-evidence-for-aai13)
records the release RAM budget, allocation attribution and exact qualification
scope. General wireless/static-memory qualification beyond that workload is not
claimed.

## Verification and contributions

Follow the [C standard](C_CODING_STANDARD.md), [test guide](tests/README.md) and
[hardware acceptance procedure](HARDWARE_ACCEPTANCE.md). Use the pinned formatter,
manual review, warnings as errors and active assertions; build affected real
ESP targets. Keep dependency pins, approved exceptions and memory caps unchanged.
Source checks, host tests, target compilation and physical observations are
separate evidence.

Keep current usage and limitations in each folder's main guide. Record fresh
builds and private bench captures under ignored `build/`; do not publish
credentials, provisioned firmware or raw private captures. Preserve the team's
attribution and Member 5's UI style. Review UI changes with the project owner.
