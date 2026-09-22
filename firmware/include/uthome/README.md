# Public firmware contracts

Modules communicate only through headers in this directory. The first
prototype will need:

| Header | Contract |
|---|---|
| `types.h` | Status codes, IDs, timestamps, and fixed limits |
| `config.h` | Parsed profile structures and validation results |
| `component.h` | Driver descriptor and component registry |
| `runtime.h` | Task, timer, queue, and lifecycle operations |
| `sensor.h` | Environment, target, and distance sample types |
| `display.h` | Display model and render request |
| `msgbus.h` | Bounded publish/consume operations |
| `health.h` | Health events, counters, and snapshots |

Interfaces use fixed-width types and bounded buffers. Public structures must
not expose ESP32-S3 SDK or μT-Kernel implementation types.
