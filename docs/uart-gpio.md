# Member 3: UART and GPIO starter and handoff

## What is ready

This is standalone, host-tested C11 code. It does not assume an ESP-IDF or
μT-Kernel API. All interfaces are provisional UART/GPIO contracts, prefixed
`ug_`; no shared headers under `firmware/include/uthome/` were changed.
Use branch `feat/uart-gpio` for this module. Commit and push from your Git
checkout after reviewing the changes.

| Location | Responsibility |
| --- | --- |
| `firmware/hal/include/uart_gpio_io.h` | UART RX/TX, digital I/O, edge callbacks, timed pulse capture, PWM and status contract |
| `firmware/hal/ports/host_sim/uart_gpio_host.*` | Deterministic virtual time, bounded UART buffers, GPIO/IRQ and PWM fake |
| `firmware/drivers/hlk_ld2450/ld2450.*` | Streaming frame recovery, three target slots, timestamp and diagnostic counters |
| `firmware/drivers/hc_sr04/hc_sr04.*` | Trigger/capture request, distance conversion, cooldown and timeout propagation |
| `firmware/drivers/pwm_motor/pwm_motor.*` | Unidirectional active-high PWM adapter; initialise and stop at zero duty |
| `tests/unit/test_uart_gpio.c` | Protocol, timeout, GPIO and actuator assertions |

From the repository folder containing `firmware`, run:

```powershell
python tools/test_uart_gpio.py
```

Requires Python 3 and GCC (or set `CC` to a Clang executable). Builds in a
temporary directory with C11 and warnings treated as errors; no firmware
toolchain or downloaded dependency is required.

## Read the code in this order

1. `uart_gpio_io.h`: a function table lets the same driver call a host fake today and
   the approved board port later. `ctx` holds the port's private state.
2. `ld2450.c`: feed bytes into a fixed 30-byte buffer; locate the header,
   verify the footer, decode each slot, return one report.
3. `hc_sr04.c`: ask the port to capture Echo around a Trigger pulse, then
   convert the duration to millimetres. Scheduling stays in the runtime.
4. `test_uart_gpio.c`: working examples of initialisation, input injection,
   reads, output control and fault handling.

## Integration contract for Members 1 and 5

Use the existing `configs/nodes/first-prototype.json` as the UART and sonar
profile. It already contains the intended LD2450 and HC-SR04 settings; avoid
introducing a second conflicting profile format.

| Existing JSON field | Member 3 binding |
| --- | --- |
| UART `port`, `rx_gpio`, `tx_gpio`, `baud_rate`, `rx_buffer_bytes` | `ug_uart_config` then `io.uart_open` |
| UART `data_bits`, `parity`, `stop_bits` | Reject anything other than 8 / none / 1 in this starter |
| Radar `timeout_ms` | Checked conversion to microseconds, then `ug_ld2450_read` |
| Radar `frame` | Validate fixed header/footer/30-byte format; not a generic configurable parser |
| Sonar `trigger_gpio`, `echo_gpio`, `timeout_us` | `ug_hc_sr04_init` |
| Sonar `interval_ms` | Runtime scheduling; require at least 61 ms |
| `destination` | Runtime publishes only successful samples to the configured queue |

The configuration layer must check pins against the exact board, all pin and
peripheral conflicts (including I²C/SPI), bounded buffer limits, and timeout
conversion overflow before starting tasks. The host's 0–63 pin range is a
test convenience, not an ESP32-S3 valid-pin list.

One task owns each driver instance and its I/O context. No function allocates
memory. UART calls allow partial transfers; callers inspect the returned byte
count. The radar reader consumes one byte at a time, so back-to-back reports
remain queued. A timeout or transport error clears its partial frame; a
malformed frame searches for the next header. A 4096-byte work cap also bounds
noise processing. Counters wrap modulo 2^32. The protocol has no report checksum,
so framing checks cannot detect every corrupted payload.

For each iteration: read -> on `UG_OK`, copy the result to the bounded queue;
otherwise publish a health event containing device ID and status. Never
publish the unchanged output buffer after an error. The driver does not create
tasks, publish messages or sleep between iterations. Convert local sample
structs to the team's shared message types once agreed. `UG_BUSY` from sonar
means the sampling cooldown has not expired; reschedule rather than spin.

Before reconfiguration, stop the owning task, quiesce interrupts, disable PWM,
and release port resources. Lifecycle/close/resource ownership will need an
agreed extension before hot reconfiguration; this starter does not supply it.

## ESP32-S3 port tasks still to implement

- Confirm the professor-approved kernel port/toolchain and run the baseline
  serial message. Do not substitute FreeRTOS APIs for μT-Kernel assumptions.
- Implement `uart_gpio_io` only under `firmware/hal/ports/esp32s3/`. Map finite
  microsecond deadlines to the approved timer and scheduler primitives.
- Implement UART configuration, buffered RX/TX and overflow reporting. Keep
  radar UART separate from the monitoring/configuration channel.
- Implement input/output setup and short ISR callbacks. Pass edge events to
  the runtime using its approved ISR-safe mechanism.
- Arm Echo capture before Trigger. Budget includes Trigger, waiting for rise
  and waiting for fall; missing/stuck Echo must time out and Trigger stays low.
  Use hardware capture or timestamped edges rather than long polling loops.
- Implement PWM with checked frequency/duty and safe initial output. Unsupported
  operations must return `UG_UNSUPPORTED`, never fake success.
- Add resource release, rollback after partial initialisation, and concurrency
  protection with the runtime owner. The host fake does not test real IRQ
  concurrency, scheduling jitter, pin routing or electrical waveforms.

## Wiring and bench checklist

Use the existing provisional map: radar TX -> GPIO16 (RX), radar RX <- GPIO17
(TX), sonar Trigger -> GPIO4, Echo -> GPIO5 through a divider. Verify the board
pinout before wiring. Both modules need their specified supply and common GND.
The project's 5 V HC-SR04 Echo must not connect directly to the MCU.

The manufacturer radar protocol describes the report structure and signed
encoding used here: [Hi-Link LD2450 protocol v1.03, section 2.3 (mirror)](https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf).
The sonar conversion and pulse timing follow the [HC-SR04 datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf).

| Bench test | Evidence to save | Acceptance |
| --- | --- | --- |
| Radar stream | Raw UART capture and decoded log | Frames and signed coordinates agree; empty slots absent |
| Radar unplug/noise | Status/counter log | Finite timeout, recovery after reconnect, no stale publish |
| Sonar at 100/500/1000 mm | Reference distance and measured values | Record error; agree tolerance with team |
| Sonar unplug/stuck Echo | Logic analyser + elapsed time | Total operation within timeout plus documented scheduler tolerance |
| Trigger interval | Logic analyser capture | Correct trigger pulse; starts separated by at least 61 ms |
| PWM 0/500/1000 | Scope capture using LED/load first | Duty and frequency correct; zero at startup and stop |
| Concurrent peripherals | Queue depth/jitter/overflow log | No unbounded waits; health events observable |

## Motor extension and demonstration

`configs/examples/pwm-motor.proposed.json` is a disabled device fragment
for review, not a deployable node profile. Its null pin intentionally prevents
accidental activation. Confirm the motor driver, usable PWM pin and frequency
before Member 5 incorporates it into the schema. Use an external motor driver
and appropriate power/protection; GPIO only supplies its control signal.
This adapter does not implement reversing, braking, ramps or current limiting.

The test demonstrates a simple sensor-value-to-duty decision and explicit stop.
A real demonstration still needs Member 1's rule and watchdog: successful fresh
distance below a chosen threshold -> configured duty; distance above threshold,
sensor fault, stale input, task stop or profile disable -> zero duty. Add
hysteresis to prevent threshold chatter. A failed stop command must generate a
health fault; software alone cannot guarantee physical shutdown.

## Suggested next deliverables

1. Member 3 + Members 1/5: review these provisional signatures, status mapping,
   sample types and ownership; record agreement before promoting shared APIs.
2. Member 3: implement the board port after baseline approval; run radar and
   sonar individually and save wiring photos, serial captures and measurements.
3. Member 1: create the reader tasks and queue/health publication; implement
   lifecycle and stale-input safe-state policy.
4. Member 5: bind validated JSON to these initialisers and reject conflicts;
   confirm the motor extension schema separately from the four-device prototype.
5. Team: integrate with OLED and rerun concurrent/fault bench tests.

Host verification covers fragmented reports, manufacturer example decoding,
bad footer recovery, consecutive reports, timeout cleanup, RX/TX overflow,
interrupt edge filtering/detach, invalid GPIO use, sonar timeout budget,
cooldown/range checks and PWM initialisation/range/stop. Passing it establishes
portable logic behaviour, not target readiness.
