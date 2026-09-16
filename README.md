# μT-Home

Configuration-driven edge platform on μT-Kernel (INF2004 Embedded Systems Programming, group AAI13).
Every node runs the **same firmware image**; a validated JSON profile assigns its role
(sensor, actuator, monitor).

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
| `firmware/drivers/` | Peripheral drivers (BME280, MPU-6050, US-100, LED, fan) behind service interfaces |
| `firmware/hal/` | Hardware abstraction API; `ports/<board>/` holds the only board-specific code |
| `firmware/third_party/` | μT-Kernel and external libraries (vendored or submodules) |
| `configs/` | Node profiles, demo scenarios, and intentionally invalid profiles for tests |
| `schemas/` | JSON schema for node profiles |
| `tests/` | Host unit tests, integration, on-target hardware and fault-injection tests |
| `tools/` | Host scripts: profile validator, provisioning, flash/monitor helpers |
| `docs/` | Architecture, use cases, wiring, test plan, measurements, meeting notes |

## Build

To be documented once the professor-approved μT-Kernel port and toolchain are confirmed.
First milestone: build, flash and run the untouched μT-Kernel baseline and print one serial message.

## Rules

- No board-specific code outside `firmware/hal/ports/`.
- Roles come from configuration, never from `#ifdef` or separate builds.
- Bounded queues, fixed task limits, timeouts, and documented actuator safe states.
- Never commit Wi-Fi credentials (`*credentials.h`, `secrets/`).
- Do not commit or publish the professor-provided FlexiEdgePico code.
