# Generic PWM output

`pwminst` exposes `pwm_output` through component configure/write operations.
It borrows a `pwmio` port; the C3 backend owns an independently timed LEDC output.
Setup and cleanup request zero duty. No actuator-specific behavior is included.
See [binding, electrical boundaries and validation](../hc_sr04/INTEGRATION.md).
