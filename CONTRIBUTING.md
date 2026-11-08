# Team workload

This proposed five-member split supports the direction agreed in the 6 November
2026 consultation. Each member owns an area and its handoffs; integration,
testing, understanding the implementation and the report are shared team
responsibilities. [README.md](README.md) describes the target workflow and its
current limits. Assign actual names as the team agrees.

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

## 3. ESPHome-compatible display — Member 3

- Own the ESPHome native API interface for Home Assistant display on the
  existing uT-Kernel firmware. The current read-only occupancy lab is a
  connectivity reference, not a general transaction-result display.
- Agree entity names, availability, raw-result presentation and freshness with
  Members 2, 4 and 5. Do not consume sensor queues or execute driver requests
  in the display endpoint; use copied data from the application owner.
- Test Home Assistant client compatibility, missing/faulted data, disconnects
  and coexistence with MQTT. Actual ESPHome-generated firmware uses a different
  kernel; communicate the comparison and chosen approach to the professor by
  **Tuesday, 10 November 2026**. Professor approval remains unrecorded.
- Handoff: native API contract, displayed entities, tests and resource costs.

## 4. On-device decision model / ML — Member 4

- Own the ESP-side decision program and its runtime-changeable rule or model
  contract. It selects how to use a generic driver based on received rules and
  an operation request; it does not need to interpret sensor values.
- Start with a small understandable rule-based approach unless the team and
  professor agree that trained parameters are required. Do not describe fixed
  rules as a trained ML model.
- Validate complete updates before applying them; define rejection behaviour
  and demonstrate that changed rules produce a changed decision without a
  rebuild or reflash. Agree serialization and ownership with Members 2 and 5.
- Test decisions, limits and switching independently, then integrate with the
  real drivers. The earlier timing-model work is historical, not this feature.
- Handoff: decision contract, implementation, test vectors and explanation of
  what can be changed at runtime.

## 5. Host configuration and system integration — Member 5

This role is recommended because the four areas above need a common host
workflow, usable example profiles and an owner for the complete demonstration.

- Prepare several concise, reviewed JSON or Markdown peripheral examples from
  datasheets. Automatic PDF conversion is out of scope. Record sources and
  represent only operations and settings the implementation can support.
- Own the host flow for selecting a peripheral and operation, producing the
  agreed configuration, and displaying returned raw bytes and status. Coordinate
  with Member 2 on transport and Member 4 on the rule format.
- Coordinate the task-based block diagram: functions, data flow, task triggers
  or schedules, and how to isolate a protocol such as UART during debugging.
  Prepare it for the professor by **Friday, 13 November 2026**.
- Coordinate end-to-end tests, demo instructions and the report with all
  members. Keep existing UI and visual style; discuss proposed UI changes before
  implementing them.
- Handoff: example profiles, host flow, diagram, integration evidence and demo.

## Integration checkpoints

1. **Agree the handoffs:** define peripheral profile, runtime rules, operation
   request and result formats; specify who validates, applies and owns each
   configuration. Each owner confirms the limits they can support.
2. **Choose the foundation:** complete the ESPHome assessment and agree the
   framework or alternative with the professor.
3. **Prove one path:** using one reviewed peripheral and supported operation,
   send a configuration from the host, make the ESP choose and run a generic
   transaction, and return the raw bytes and status.
4. **Show change at runtime:** update the rules while the ESP is running, repeat
   the request and show the changed ESP decision without a firmware rebuild or
   reflash. Verify rejected updates do not silently replace the working rules.
5. **Broaden and report:** add the agreed examples and protocols, test the
   integrated system and document actual observations, limitations and results.

Every member follows [BARR-C:2018 project requirements](C_CODING_STANDARD.md)
for project-owned C and completes the required checks in [AGENTS.md](AGENTS.md).
Record host tests, target builds and hardware observations separately. The
existing [hardware acceptance guide](HARDWARE_ACCEPTANCE.md) is retained for
reviewed wiring and historical procedures; it is not a claim that the new
workflow has passed acceptance. Discuss blockers promptly with the professor.
