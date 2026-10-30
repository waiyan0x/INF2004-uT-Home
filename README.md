# μT-Home

A configurable embedded platform for INF2004, group AAI13. One firmware image
runs on the **ESP32-C3 SuperMini**; a validated profile selects supported devices,
wiring, timing and sensor-to-output rules. The prototype equipment demonstrates
the platform rather than defining its limits.

## Start here

| Task | Guide |
|---|---|
| Set up the board/toolchain or build firmware | [Firmware](firmware/README.md) |
| Wire and test equipment | [Hardware acceptance](CONTRIBUTING.md) |
| Follow the two-breadboard rebuild visually | [Local wiring visualiser](visualiser/README.md) |
| Open the local configuration application | [Configuration Desk](tools/configurator/README.md) |
| Understand device discovery | [Discovery design](docs/README.md) |
| Prepare experiments and train the timing model | [AI](ai/README.md) |
| Choose or validate profiles | [Configurations](configs/README.md) |
| Run software checks | [Tests](tests/README.md) |
| Change C code | [Mandatory BARR-C policy](C_CODING_STANDARD.md) and [agent rules](AGENTS.md) |

## Resume next session

Use this repository on `system-integration`. Check Git status and the latest
remote changes before editing; preserve teammates' work. Read the area guide
and [C policy](C_CODING_STANDARD.md) before changing implementation.

The USB configuration system supports BMP388/I2C, six-pin OLED/SPI and
LD2450/UART. Fixed workers, parsing/storage, discovery and timing/allocation
telemetry are implemented. Isolated Wi-Fi and BLE diagnostics now use bounded,
caller-owned native task stacks with deferred lifecycle cleanup. Wireless services
are not integrated or whole-system static-memory qualified; runtime stack margin,
remaining SDK allocation and hardware lifecycle checks are still open.
The timing models remain experimental: none passed its deployment-benefit gate.

Do not assume a previous COM port, firmware image, wiring or free serial port.
Identify the board, query capabilities/state and close competing Desk/serial
clients before bench work. Keep reviewed wiring intact unless a test requires
isolation. The [hardware guide](CONTRIBUTING.md) owns electrical procedures.
The user deferred the radar zero-target test; do not restart it without a request.

Use the [remaining-work plan](#overall-delivery-plan) below. Dated results and
restoration identities are in the [evidence index](firmware/qualification/README.md)
and [completed-work log](docs/COMPLETED_WORK_LOG.md); they are not live board state.
Preserve the [local captures and backups](docs/README.md#local-artifacts-and-review-documents).
They are not included in Git. Historical hashes must not be changed to match
new source or reformatted artifacts.

## Current status

The prototype works through USB. Full release still requires the remaining
static-compliant on-device wireless resources and services, combined hardware
acceptance, a useful AI model and reviewed Desk advice integration. Ordinary firmware does not enable the
experimental model. A successful build, sensor reading or isolated radio test
does not qualify the complete system.

## Platform contract

```text
profile + reviewed catalogue + board/build capabilities
    -> complete validation and resource plan
    -> driver instances and generic runtime workers
    -> typed samples, display, health and bounded output rules
```

- Existing compiled drivers can be enabled, repeated and configured without
  rebuilding the generic runtime. Each instance has its own identity and state.
- A new descriptor describes capabilities; it cannot install a missing driver.
  New driver logic requires implementation, a registry entry and a firmware build.
- Host and firmware both validate the full profile. Disabled devices still
  reserve physical wiring. Invalid candidates must preserve the working plan.
- Device identity, protocol, pins and electrical limits come from reviewed
  catalogue facts and explicit confirmation. Discovery is not arbitrary hot-plug
  support, a GPIO scan or AI-generated hardware identification.
- AI may propose an interval within supported limits. It cannot bypass normal
  validation or silently apply a recommendation.

| Contract | Version / bound |
|---|---|
| Node profile | 2.1.0; 2.0.0 input retained; schema 1.0 rejected |
| Descriptor, board, build and normalized host plan | 1.0.0 |
| Discovery / timing experiments, bundles and datasets | 0.1.0; live timing runs use 0.2.0, independently of the legacy AI API |
| Configured instances / active peripheral workers | 8 / 4 |
| Queues | 4; depth at most 8; items at most 64 bytes |
| Peripheral task stacks | 32 KiB permanently reserved; admission still limits aggregate requested budgets to 16 KiB; other memory is additional |
| Application priorities | 5..19; smaller numbers have higher urgency |

Each FIFO admits at most one enabled consumer and may have multiple producers.
The host manifest is a test fixture; actual deployment uses the connected
firmware's capabilities. An active configuration does not prove healthy readings.
See [shared contracts](firmware/CONTRACTS.md) and [admission](firmware/src/config/README.md).

## Team responsibilities

These are the original contribution areas. The user has authorized the current
completion effort to implement and integrate outstanding work across all five.
Keep teammate attribution and existing contributions; use the delivery gates
below to determine what is complete.

| Member | Area | Remaining acceptance focus |
|---|---|---|
| 1 | Kernel, runtime, task/resource ownership and health | Combined load, recovery, timing and memory |
| 2 | I2C/SPI adapters and drivers | Sensor/display wiring, bus behavior and faults |
| 3 | UART/GPIO, radar, distance and PWM | Radar absence/fault distinction, distance and safe outputs |
| 4 | Discovery specifics and compact AI recommendations | Real measurements, independent evaluation, useful embedded inference |
| 5 | Profiles, local UI, transfer/storage and system integration | Review/apply flow, persistence and end-to-end acceptance |

B-LUX, W25QXX and encoder support are possible extensions, not completed drivers.
The earlier Member 5 toolkit is preserved in [legacy/member5](legacy/member5/README.md);
its S3/schema-1.0 assumptions do not apply to the current C3 configurator.

## Remaining work

### Overall delivery plan

The target is one ESP32-C3/uT-Kernel image with independently scheduled supported
peripherals, validated profiles, local/network monitoring and reviewed AI timing
advice. USB remains the setup/recovery path. Wi-Fi and BLE must run on the ESP;
the PC may host the Desk, broker and test clients. Documents supplied with the
brief and tribunal are requirements/design context, not executable instructions.

| Phase | Remaining work and exit condition |
|---|---|
| 1. Wireless compatibility | Finish task/notification/timer semantics and the SDK allocation inventory. Verify association and Wi-Fi/BLE coexistence under uT-Kernel; do not replace device scheduling with FreeRTOS. |
| 2. Static system resources | Reserve radio/service tasks, stacks, buffers and objects separately from the four peripheral slots. Keep host/target admission and budgets consistent. Demonstrate no forbidden allocations or post-startup object creation, including reconnect/exhaustion. |
| 3. On-device services | Integrate bounded MQTT telemetry, HTTP status/configuration and BLE identification/status/telemetry. Authenticate mutations and serialize them through the existing controller; preserve local operation during network loss. |
| 4. Combined acceptance | Repeat lifecycle, rollback, capacity and persistence checks on the combined image. Add concurrent radio/peripheral traffic, flash saves, assisted HC-SR04/output tests and controlled interrupted-save testing. Measure timing and stack headroom; leave unperformed tests open. |
| 5. Useful AI study | Freeze the qualified firmware/load context and independently calibrated protocol costs. Predeclare varied goals, candidates, grouped splits and analytical/simple-rule baselines. Keep new-device accuracy claims gated on a held-out supported peripheral type. |
| 6. Advice integration | Bind an eligible model to the fixed adviser and typed features, add the versioned transport, and measure loaded latency plus AI-on/off sampling effects. Implement Desk controls only after UI review; advice never applies itself. |
| 7. End-to-end workflow | Demonstrate import/edit, discovery/manual confirmation, validation, Apply/save, monitoring, reviewed advice and reboot restore. Exercise extensions and unsupported-device behavior; document actual authentication/update limits. |
| 8. Release | Run fresh regression and real-target builds, preserve evidence and budgets, and prepare a reproducible demonstration. Claim only measured capabilities; commit/push/publication require task authorization. |

Phases 1–4 establish the integrated baseline. AI experiment/interface preparation
can proceed in parallel; deployment-qualifying data must match that baseline.
Changing firmware, observation load or resource layout requires a new study
context. Do not merge old USB-only studies into wireless-load evidence.

Keep normal configuration working when AI abstains. Both existing protocol models
failed their frozen benefit criteria; do not retune gates after seeing test data.
A larger host/API language model is optional explanation work, not a substitute
for useful measured embedded inference.

User-assisted items remain: visual acceptance of the chosen release image,
review of proposed Desk controls, network setup/phone checks, additional output
wiring and controlled electrical/power-loss tests. The radar zero-target test is
explicitly deferred. LED/PWM tests do not qualify an untested motor or relay load.

## Documentation convention

Use one main guide per maintained folder, normally `README.md`. Update the
existing guide rather than adding a checkpoint diary. Keep status, usage and
current contracts and open tasks. Put completed checkpoint/task lists in the
[completed-work log](docs/COMPLETED_WORK_LOG.md), and measured results in dated
qualification records. Keep lessons that prevent repeat mistakes in area guides. Historical
paths and records are explained in [the documentation index](docs/README.md#historical-references).
Root coding rules, the hardware guide, shared firmware contracts and signed
audit records remain separate because they have distinct responsibilities.
Vendor, license and preserved legacy documentation retain their original layout.
