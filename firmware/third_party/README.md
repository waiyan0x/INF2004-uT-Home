# Third-party software

Add external code only after the exact dependency is approved. The first
prototype expects:

- the course-approved μT-Kernel port;
- a bounded JSON parser suitable for the ESP32-S3;
- any vendor component required by the confirmed ESP32-S3 toolchain.

For each dependency, record its source URL, exact version or commit, licence,
local modifications, and update procedure. MQTT is outside the first prototype
and should not be added yet.


# BMP3_SensorAPI (vendored)

Bosch Sensortec BMP3 pressure sensor driver, used unmodified by the BMP388
driver in `firmware/drivers/bmp388/`.

- Source: https://github.com/boschsensortec/BMP3_SensorAPI
- Version: v2.0.6
- Files: `bmp3/bmp3.c`, `bmp3/bmp3.h`, `bmp3/bmp3_defs.h`
- Licence: BSD-3-Clause (see the header in each file)

Do not edit these files. Update by replacing them with a newer release and
updating the version above. The compensation build defaults to
`BMP3_FLOAT_COMPENSATION`, so `bmp3_data` reports pressure in pascals and
temperature in degrees Celsius.
