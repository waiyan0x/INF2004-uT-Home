# ESP32-C3 network adapters

This folder contains the SDK/kernel boundaries for the experimental network
compatibility layer. Portable queue, event and wait policy lives in
[`firmware/src/network`](../../../../src/network/README.md), which owns the
resource limits, build instructions, manual reviews and qualification evidence.

| Modules | Responsibility |
|---|---|
| `c3boot` | Bounded native startup pool for selected static-network builds |
| `c3nq`, `c3event` | Native queue/event synchronization and fixed resource bindings |
| `c3wifi` | Isolated Wi-Fi scan diagnostic |
| `c3ble`, `c3gatt` | Isolated BLE advertising and read-only GATT diagnostics |
| `c3bport` | Typed kernel boundary that keeps kernel headers out of NimBLE units |

The target qualifier stages these files into its existing flat HAL layout and
keeps the separate radio component's include namespace. Peripheral, USB and
configuration-storage adapters remain in the [parent port](../README.md).
These sources do not establish combined wireless acceptance or static-memory
qualification. Historical evidence retains its original source paths and hashes.
