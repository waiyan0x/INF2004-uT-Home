# BMP388 driver

The software integration is implemented; physical acceptance is pending wiring.
See [INTEGRATION.md](INTEGRATION.md) for modules, registry binding, ownership,
deadlines, provenance, tests and the hardware acceptance boundary.

The BMP388 is the first prototype's I²C environmental sensor. The driver owns
device-specific register access and compensation while using only the public
I²C HAL.

Input from configuration:

- interface reference and 7-bit address (`0x76` in the prototype);
- sampling interval and whole-operation timeout;
- destination queue.

Output is an `environment_sample` containing pressure in pascals, temperature
in degrees Celsius, a monotonic timestamp, and status. Initialisation must
verify the chip identity. Bus errors and timeouts are reported to the health
service; they must not block the other device tasks.
