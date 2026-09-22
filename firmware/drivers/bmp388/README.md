# BMP388 driver

The BMP388 is the first prototype's I²C environmental sensor. The driver owns
device-specific register access and compensation while using only the public
I²C HAL.

Input from configuration:

- interface reference and 7-bit address (`0x76` in the prototype);
- sampling interval and transaction timeout;
- destination queue.

Output is an `environment_sample` containing pressure in pascals, temperature
in degrees Celsius, a monotonic timestamp, and status. Initialisation must
verify the chip identity. Bus errors and timeouts are reported to the health
service; they must not block the other device tasks.
