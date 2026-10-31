# UART and GPIO integration

The production target is ESP32-C3. Use the maintained [radar driver](../firmware/drivers/hlk_ld2450/INTEGRATION.md),
[distance/PWM guide](../firmware/drivers/hc_sr04/INTEGRATION.md),
[C3 HAL](../firmware/hal/ports/esp32c3/README.md) and
[hardware layouts](../CONTRIBUTING.md). Earlier S3 starter tasks, provisional
`ug_` sensor APIs and GPIO16/17 wiring are archived in the
[completed-work log](COMPLETED_WORK_LOG.md); they are not deployment instructions.

## Modules and tests

`ugio.h`, `ughost.*` and `pwm_motor.*` retain standalone host GPIO/PWM fixtures.
They do not replace the production `uartio`, `ld2450`, `hc_sr04` and C3 adapters.
Run `python tools/test_uart_gpio.py` with GCC and pinned clang-format available;
the maintained runner uses C99 with warnings as errors. Host fake pin ranges
and virtual clocks are test conveniences, not board capabilities.

## Production contract

- Validate complete profiles against the board, compiled drivers, pin conflicts,
  fixed capacity and timeout bounds. The historical `first-prototype.json` is
  not a deployable C3 wiring plan.
- UART1 belongs exclusively to native DEV_SER on RX20/TX21. Keep its descriptor,
  fixed records and synchronization for the boot lifetime; see
  [retained ownership](uart-startup-allocation.md) and [storage](uart-static-storage.md).
- One worker owns each driver/context. Inspect partial-transfer counts and errors;
  never publish an unchanged output buffer as a new successful sample.
- Radar framing has no checksum and cannot detect every corrupted payload.
  Fresh zero-target reports are valid; stale transport and scene absence differ.
- Arm sonar capture before trigger. The deadline covers trigger and waiting for
  both edges; timeout must leave trigger low. Respect the maintained driver's
  cooldown rather than spinning on BUSY.
- Quiesce workers and interrupts before releasing/rebinding resources. Failed
  cleanup retains ownership. PWM starts/stops at zero; stale/fault input requests
  safe output through the existing rule system. Failed writes remain faults,
  not proof of electrical shutdown.

## Remaining acceptance

Follow [UART acceptance](uart-acceptance.md) for external baud/framing, owner
changes, overflow and failure tests. Verify sonar voltage/pulses against known
distances, output frequency/duty/startup-zero on LED/scope loads, and combined
load timing. Never connect 5 V HC-SR04 Echo directly to the ESP. The current
OLED and HC-SR04 layouts conflict on GPIO4 and must be tested separately.
Radar zero-target acceptance is deferred at the user's request.

Motor operation requires a reviewed external driver, supply and protection.
The [motor guide](../firmware/drivers/pwm_output/README.md) covers fixed-direction
PWM only; reversing, braking, ramps and current limiting are not implemented.
Do not treat a disabled proposed fragment as a deployable node profile or a
host test as physical load acceptance.
