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
| Open the local configuration application | [Configuration Desk](tools/configurator/README.md) |
| Understand device discovery | [Discovery design](docs/README.md) |
| Prepare experiments and train the timing model | [AI](ai/README.md) |
| Choose or validate profiles | [Configurations](configs/README.md) |
| Run software checks | [Tests](tests/README.md) |
| Change C code | [Mandatory BARR-C policy](C_CODING_STANDARD.md) and [agent rules](AGENTS.md) |

## Current status

| Area | Status |
|---|---|
| Runtime, HAL, supported drivers, profile transfer/storage, telemetry and rules | Implemented; software and target-build evidence retained |
| Existing local UI and reviewed I2C/UART/manual discovery | Implemented; physical discovery acceptance pending |
| Basic BMP388, OLED and radar operation | Observed on the bench; full acceptance remains incomplete |
| Offline AI pipeline | Simulated runs, training, evaluation and C export implemented |
| Embedded AI deployment | Pending; the current simulated model has not improved on the simple timing rule |

The 2026-10-17 pre-push checks passed 250 local tests and the 194-file BARR-C gate.
The two saved C3 build snapshots matched the firmware sources. These checks do
not establish physical reliability or measured AI performance. Details and
historical evidence are indexed in [firmware qualification](firmware/qualification/README.md)
and [AI results](ai/README.md#results-and-remaining-work).

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
| Discovery and current timing contracts | 0.1.0, independent of legacy AI 0.2.0 |
| Configured instances / active peripheral workers | 8 / 4 |
| Queues | 4; depth at most 8; items at most 64 bytes |
| Peripheral task stacks | At most 16 KiB total; other memory is additional |
| Application priorities | 5..19; smaller numbers have higher urgency |

Each FIFO admits at most one enabled consumer and may have multiple producers.
The host manifest is a test fixture; actual deployment uses the connected
firmware's capabilities. An active configuration does not prove healthy readings.
See [shared contracts](firmware/CONTRACTS.md) and [admission](firmware/src/config/README.md).

## Team responsibilities

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

1. Complete physical discovery and checkpoint-8 acceptance: stationary radar
   absence/fault tests, HC-SR04, PWM/outputs, combined load, recovery and power loss.
2. Implement actual build/profile-correlated recording and collect real timing
   runs. Keep related runs together and reserve untouched test groups.
3. Retrain and compare recommendations with defaults and simple rules. Simulation
   results do not establish sensor behavior, power savings or real accuracy.
4. Integrate inference only if it provides useful benefit; then measure its
   latency, memory use and behavior on the ESP.

## Documentation convention

Use one main guide per maintained folder, normally `README.md`. Update the
existing guide rather than adding a checkpoint diary. Keep status, usage and
current contracts; put measured results in dated evidence records. Historical
paths and records are explained in [the documentation index](docs/README.md#historical-references).
Root coding rules, the hardware guide, shared firmware contracts and signed
audit records remain separate because they have distinct responsibilities.
Vendor, license and preserved legacy documentation retain their original layout.
