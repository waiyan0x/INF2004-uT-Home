# Third-party software

Add external code only after the exact dependency is approved. The first
prototype expects:

- the course-approved μT-Kernel port;
- a bounded JSON parser suitable for the ESP32-S3;
- any vendor component required by the confirmed ESP32-S3 toolchain.

For each dependency, record its source URL, exact version or commit, licence,
local modifications, and update procedure. MQTT is outside the first prototype
and should not be added yet.
