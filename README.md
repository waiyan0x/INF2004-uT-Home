# μT-Home

A configurable embedded platform for INF2004, group AAI13. The project aims
to let a user change how an ESP32-C3 communicates with peripherals at runtime,
without recompiling or reflashing the firmware for each change.

## Resume next session

The direction below follows the 6 November 2026 consultation. The end-to-end
workflow defines the target scope. The bounded three-bus service below is
implemented and software-tested; complete hardware acceptance remains pending. Check Git status, read the relevant area guide, and preserve existing
work and historical evidence before making changes. The five-member ownership
and handoffs are in [CONTRIBUTING.md](CONTRIBUTING.md).

Wi-Fi now connects to the tested iPhone hotspot on the C3 using the optional
8.5 dBm transmit-power cap. The 11 November uT-Kernel application preserves
active/saved revision 931, provides read-only USB `network` diagnostics and
passes a native API identity/entity query over Wi-Fi. MQTT TLS acceptance and
whole-system static-allocation qualification remain open. The combined discovery
build compiles but exceeds the existing MQTT module reservation by three bytes.
See [Wi-Fi status and scoped bench results](firmware/src/network/README.md#usb-wi-fi-troubleshooting).

The selected direction is **uT-Kernel firmware with an ESPHome-compatible native
API interface for Home Assistant display**. The transaction service implements
bounded generic I2C, SPI writes and UART operations, MQTT rule reception and
application-owner execution. The native API exposes a separate Transaction
result text entity alongside radar occupancy/freshness. These paths have software
integration tests; physical three-bus, radio and Home Assistant acceptance remain
pending. Use the [transaction service instructions](experiments/runtime_rules/README.md#running-the-integrated-service). The framework comparison and
USB rule-switching proof are in the [runtime-rule guide](experiments/runtime_rules/README.md);
the choice has
not yet been recorded as professor approval. Prepare a task-based block diagram by the end
of the following week (13 November): show functions, data flow, triggers or
periods, and how an individual protocol can be isolated for debugging. These
are commitments from the consultation, not completed milestones.

<a id="platform-contract"></a>

## Target workflow

1. Prepare a few concise, reviewed JSON or Markdown peripheral descriptions
   based on datasheets. They contain the communication details needed for the
   supported examples. Automatically extracting PDFs is outside this scope.
2. On the host, the user selects a peripheral and an operation such as read or
   write. Host code turns the selected description and operation into a
   configuration or set of rules for the ESP.
3. The host sends the configuration over Wi-Fi using MQTT. The intended ESP
   decision program validates and applies the rules while running, then decides
   how the appropriate generic I²C, SPI or UART driver should communicate.
4. The generic driver performs the configured transaction. The ESP returns raw
   response bytes and status to the host over MQTT; the user interprets their
   meaning.
5. Send different rules and demonstrate a changed transaction without
   rebuilding or reflashing the ESP. Show the active configuration and the
   actual decision and result so the runtime change is observable.

The decision component can initially be a configurable rule-based program;
it does not need trained weights unless the team and professor later require
them. It must select a transaction on the ESP from runtime inputs and rules;
calling a fixed transaction a trained ML model would be misleading. The bounded `TJ` message contract, fixed wiring, ownership and supported transfer
shapes are documented in the runtime-rule guide. This is a runtime rule-based
decision program, not a trained classifier.

<a id="current-status"></a>

## Existing work and boundaries

The repository contains a USB-configured prototype, device-specific drivers,
runtime, configuration tools, networking work and a runtime-rule prototype.
They are possible building blocks, not proof of the target workflow. The retired timing-model experiments did not demonstrate decisions about
peripheral transactions; their historical qualification records are retained. Assess reuse against the new contracts rather
than assuming existing modules or their qualification apply unchanged.

One firmware image and runtime updates are goals; supported buses, transfer
shapes and limits must be stated and tested. Raw data accuracy or decoding is
not the demonstration objective, but transaction status and limitations must
be reported honestly. Do not assume hardware, a serial port, a firmware image
or wiring is unchanged. Separate host checks, board observations and completed
system acceptance. Preserve dated qualification records, hashes, local captures
and backups; do not rewrite historical evidence to imply current results.

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
tools/          Host configuration, USB/network clients, build and C policy tools
visualiser/     Wiring/plan visualiser and its local server
catalogue/      Reviewed boards, peripherals and connection presets
configs/        Example node profiles, scenarios and build selections
schemas/        JSON contracts used to validate those inputs
datasheets/     Reviewed peripheral communication notes and source references
tests/          Software suites, board fixture and hardware test procedures
docs/           Cross-project guides, hardware notes and development history
experiments/    Isolated proofs and comparison tools with explicit limitations
reports/        Scoped resource studies and handoff reports
ai/             Historical AI qualification and its limitations
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
| Existing AI work and historical experiments | [AI](ai/README.md) |
| Existing firmware contracts | [Shared contracts](firmware/CONTRACTS.md) |
| Software tests | [Tests](tests/README.md) |
| Required C policy | [BARR-C project standard](C_CODING_STANDARD.md), [agent rules](AGENTS.md) |

All new or changed project-owned C must follow BARR-C:2018 and the project
standard, including the pinned formatting/checker gate, manual review and
affected tests. Target changes require the applicable real-target build and new
evidence. The team is responsible for understanding and reviewing all code,
including code produced with AI assistance. Document testing and results in the
project report and raise blockers with the professor promptly.
