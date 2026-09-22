# Firmware

The first prototype runs one reusable firmware image on an ESP32-S3 SuperMini.
At boot, a validated JSON profile selects interfaces, drivers, queues, task
parameters, and enabled devices. Changing a sampling interval or enabling a
device must not require rebuilding the image.

## Prototype data flow

```text
JSON profile
    ↓
configuration manager → interface and pin allocation
    ↓
driver registry → one task per enabled peripheral
    ↓
bounded queues with drop-oldest telemetry policy
    ↓
OLED display service + USB serial monitoring + health reporting
```

The prototype instantiates four device tasks:

| Task | Driver | Interface | Queue use |
|---|---|---|---|
| `bmp388_reader` | BMP388 | I²C | Publishes `telemetry.environment` |
| `oled_renderer` | SSD1306-compatible OLED | SPI + GPIO control | Consumes all telemetry and health queues |
| `ld2450_reader` | HLK-LD2450 | UART | Publishes `presence.targets` |
| `hc_sr04_reader` | HC-SR04 | GPIO + timer | Publishes `distance.front` |

The OLED controller is provisional until the physical module is checked.

## Module boundaries

- `app/` owns boot and top-level orchestration.
- `include/uthome/` contains public, hardware-independent contracts.
- `src/config/` parses, validates, stages, and activates profiles.
- `src/runtime/` owns μT-Kernel tasks, timers, queues, and lifecycle changes.
- `src/services/` normalises sensor data and creates the display view.
- `src/messaging/` provides the local bounded queue abstraction.
- `src/health/` records task, queue, driver, and configuration faults.
- `drivers/` contains peripheral-specific logic that uses only the HAL.
- `hal/` contains protocol contracts and target or host implementations.
- `third_party/` contains approved external code with recorded provenance.

`src/automation/`, actuator services, and remote transports remain outside the
first prototype path.

## Implementation gate

Do not add build files or source that guesses the μT-Kernel API. First confirm
the approved kernel port, ESP32-S3 toolchain, startup model, and JSON library.
After that baseline builds and prints through USB serial, implement the public
contracts and host fakes before integrating the four target drivers.
