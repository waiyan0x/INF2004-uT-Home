# Actuator services

This directory is a placeholder for higher-level command validation and actuator
feedback. Current PWM/rule behavior is implemented in the
[automation modules](../../automation/README.md) and
[PWM component](../../../drivers/pwm_output/README.md).

Physical output/load acceptance remains open. Future services must reuse checked
configuration, ownership and safe-state handling rather than introduce another
output path. No actuator feedback implementation is provided here.
