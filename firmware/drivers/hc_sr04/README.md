# HC-SR04 driver

The HC-SR04 uses one GPIO output for Trigger and one timed GPIO input for Echo.
The driver emits a trigger pulse, measures the Echo pulse through the GPIO/time
HAL, converts it to distance, and publishes a `distance_sample`.

The 5 V Echo output must pass through the voltage divider shown in the
prototype before reaching the ESP32-S3. This is a hardware requirement and
cannot be replaced by software validation.

A missing Echo edge or a pulse beyond `timeout_us` returns a timeout status and
publishes a health event. Measurement loops must yield between configured
sampling intervals.
