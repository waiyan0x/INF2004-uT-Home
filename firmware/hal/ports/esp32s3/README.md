# ESP32-S3 port placeholder

This folder has no implemented HAL port. The maintained target is
[ESP32-C3](../esp32c3/README.md); use its reviewed pin map and build guide.
The earlier S3 prototype mapping is historical, not wiring guidance for C3.

A future S3 port must own pin configuration, handles, interrupt glue, timing,
storage and transport behind the public HAL. Drivers and services must not
include chip-specific SDK headers directly.
