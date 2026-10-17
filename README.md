# μT-Home

Configuration-driven edge platform on μT-Kernel (INF2004 Embedded Systems Programming, group AAI13).
Every node runs the **same firmware image**; a validated JSON profile assigns its role
(sensor, actuator, monitor).

Project-owned C follows [BARR-C:2018](C_CODING_STANDARD.md). The standard gate,
affected tests and manual review checklist are required for every C change.
See [AGENTS.md](AGENTS.md) for instructions for coding agents.

## Repository layout

| Path | Purpose |
|---|---|
| `firmware/include/uthome/` | Public, hardware-independent APIs (the contracts between modules) |
| `firmware/src/runtime/` | μT-Kernel task, queue, timer and resource-pool wrappers |
| `firmware/src/config/` | Profile loading, validation, component registry, last-known-good storage |
| `firmware/src/roles/` | Sensor / actuator / monitor role start-up selected from the profile |
| `firmware/src/services/` | Logical sensor and actuator services |
| `firmware/src/automation/` | Rule engine: thresholds, hysteresis, schedules, stale-data policy |
| `firmware/src/messaging/` | Publish/subscribe message bus and its transports (MQTT, loopback) |
| `firmware/src/health/` | Task liveness, jitter, queue depth, node loss, safe-state triggers |
| `firmware/drivers/` | Prototype drivers (BMP388, SSD1306 SPI OLED, HLK-LD2450, HC-SR04) behind service interfaces |
| `firmware/hal/` | Hardware abstraction API; `ports/<board>/` holds the only board-specific code |
| `firmware/third_party/` | μT-Kernel and external libraries (vendored or submodules) |
| `configs/` | Node profiles, demo scenarios, and intentionally invalid profiles for tests |
| `schemas/` | Generic node and device-descriptor contracts |
| `catalogue/` | Reviewed device capabilities and board resource limits |
| `ai/` | Offline timing experiments, simulated training/evaluation and experimental C export |
| `tests/` | Host unit tests, integration, on-target hardware and fault-injection tests |
| `tools/` | Host scripts: profile validator, provisioning, flash/monitor helpers |
| `docs/` | Architecture, use cases, wiring, test plan, measurements, meeting notes |

## Project baseline

[PROJECT_BASELINE.md](PROJECT_BASELINE.md) defines the reusable platform:
reviewed device descriptors, board/build capabilities, generic instance
configuration, driver registration and bounded runtime admission. The original
prototype is one example deployment. Alternative configurations and host
acceptance tests exercise adaptation independently of AI. The kernel task example
passed startup and repeated USB-reset checks on the ESP32-C3 on 2026-09-28.
Application runtime, peripheral drivers, profile transfer/storage, telemetry,
local configuration and rule routing are implemented in software. Reviewed
device discovery is also implemented, with physical discovery and full platform
acceptance still pending. Earlier basic BMP388, OLED and radar checks do not
replace that acceptance work.

The [offline AI pipeline](ai/OFFLINE_PIPELINE.md) supports simulated recordings,
training, evaluation and C export. Its current timing model has not demonstrated
an advantage over the simple rule and is not linked into the firmware. Real
measurement collection, retraining and evaluation remain pending; firmware
integration follows only if the model provides a useful benefit. Device identity,
protocols and wiring recommendations come from reviewed catalogue facts.

[INTEGRATION_PLAN.md](INTEGRATION_PLAN.md) tracks the remaining member work and
review checkpoints. [Firmware contracts](firmware/CONTRACTS.md) define the shared
sample records, ownership rules and resource accounting for that integration.

## Build

The prototype targets the **ESP32-C3 SuperMini**, identified from the physical
board. The selected μT-Kernel port is
[sirfonzie/esp32c3-mtk3](https://github.com/sirfonzie/esp32c3-mtk3), pinned
as a submodule under `firmware/third_party/esp32c3-mtk3/`.

Follow [the C3 setup guide](firmware/SETUP_ESP32C3.md) to obtain the source,
activate ESP-IDF, and build the upstream task example. The guide records the
exact kernel revision, [board evidence](firmware/qualification/README.md), and
remaining hardware checks. The configuration application has a separate build
mode; see [USB/storage integration](firmware/src/config/USB_STORAGE.md).
Successful compilation is separate from physical hardware acceptance.

## Rules

- Keep team-owned board access in `firmware/hal/ports/esp32c3/`; isolate
  kernel calls in the runtime adapter. The pinned upstream dependency
  retains its own board and kernel layout.
- Roles come from configuration, never from `#ifdef` or separate builds.
- Bounded queues, fixed task limits, timeouts, and documented actuator safe states.
- Never commit Wi-Fi credentials (`*credentials.h`, `secrets/`).
- Do not commit or publish the professor-provided FlexiEdgePico code.

## Local configurator and generic rules

Run `python -B -m tools.configurator` from this directory and open
`http://127.0.0.1:8080`. See [setup and deployment semantics](tools/configurator/README.md).
Schema 2.1 adds [typed sensor-to-PWM rules](firmware/src/automation/README.md),
retaining 2.0 sensor/display profiles. The latest sections of
[the implementation plan](INTEGRATION_PLAN.md) record completed discovery and
offline AI work, alongside the remaining hardware and AI deployment checkpoints.

## Earlier prototype toolkit

Member 5's schema-1.0/S3 toolkit, including recommendation and health imports,
is preserved with its examples and tests under [legacy/member5](legacy/member5/README.md).
Use the root C3 configurator and acceptance guide for the current board.
