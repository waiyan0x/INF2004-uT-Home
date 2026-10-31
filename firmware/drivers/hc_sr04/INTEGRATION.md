# HC-SR04 and GPIO/PWM integration

Reusable pulse capture and PWM contracts, C3 backends and registry adapters.
Software/build checks passed; physical pulse timing and output acceptance remain
open. Member 3 provenance: cached commit
`974eb9c2e3792a1be70a64a4ba7fa9c322ba150d` (`kimNewpsysk1t`).
Portable interfaces, lifecycle cleanup and C3 backends are owned adaptations.

## Modules and binding

| Module | Responsibility |
|---|---|
| `digiin`, existing `digiout`, `c3pin` | Borrowed level callbacks and shared exclusive pin claims |
| `pulseio`, `c3pulse`, `gpiofake` | One owned trigger/input pair, bounded capture, clock and fault injection |
| `hc_sr04`, `hcinst` | Sensor policy/conversion and registry/queue/health integration |
| `pwmio`, `c3pwm`, `pwminst` | Owned PWM channel and generic component writes |

Borrow `c3pulse_port(0)` or `(1)` without I/O; each statically allocated port owns
one TX and one RX RMT channel. Zero-initialize `hcinst_t`, set `p_port`, register
`hcinst_descriptor()` as `hc_sr04`, and pass `hcinst_config_t` to configure.
Use `hcinst_step` and `hcinst_cleanup` in the existing periodic activation plan.
The queue must accept `UTHOME_SAMPLE_DISTANCE` / `sample_distance_t`.

The binding layer must admit pins, channel count, queue type/capacity, timing and
resources before activation. The generic catalogue's four-instance limit is
not a C3 guarantee: this backend supports at most **two capture pairs**, subject
to available pins. Production admission and USB/JSON deployment are implemented
in [configuration](../../src/config/README.md).

Borrow `c3pwm_port(0..3)`, set `pwminst_t.p_port`, and register
`pwminst_descriptor()` as `pwm_output`. Configure with `pwmio_config_t`; send
`pwminst_command_t` through `component_write`. Writes require one serialized
command owner; lifecycle cleanup runs with users stopped. There is no idle
worker, motion policy, direction control or feedback loop. The production binder adds typed-rule admission and exclusive command ownership;
see [automation](../../src/automation/README.md). Compiled support does not
certify hardware operation.

Ports and contexts are borrowed, noncopyable, and must outlive every user.
Open/close are serialized lifecycle-owner operations. Partial setup retains its
claim; failed cleanup prevents reuse until checked recovery succeeds. A failed
duplicate driver open cannot close another instance's port. RMT and LEDC are
reserved exclusively for these adapters; no SDK peripheral driver may also own
them. Shared clocks reset only for the first user and stop after the last one.

## Capture timing and distance records

`pulseio` accepts a 1..100 us trigger and 100..30000 us total capture budget.
The C3 backend uses XTAL-derived 1 MHz RMT timing. A hardware TX symbol generates
the short pulse and returns low independently of task scheduling. It arms RX
first, rejects an initially high input and yields with `tk_dly_tsk(1)` while
waiting for a complete symbol. It never polls GPIO edges in a busy loop.

RMT starts timing at its first edge. The backend briefly selects an internal
constant-high matrix input, enables RX, then selects the actual low Echo input.
That internal falling edge starts the low half of the symbol; the real echo
rise/fall completes its high half. This does not drive the physical Echo pin.
Only two fixed 2 us settling delays busy-wait. RAM is read only after stopping
RX and returning memory ownership to software. The C3 TRM defines four aligned
48-word banks at RMT base + 0x400; the wrapper contains the sole MMIO cast.

The read-only SDK microsecond counter is used only for capture deadlines. The
pinned startup initializes it before the native kernel starts; native ticks use
the other SYSTIMER counter. No SDK timer/task is created. Deadlines include
setup, trigger and both edges; a second check after reading rejects late data.
Scheduler delay can postpone timeout reporting and conservatively discard a
completed echo. Sub-millisecond budgets have no scheduling-latency guarantee.
Every completed measurement path stops capture/transmit and leaves trigger low.
Contention returns immediately while the current owner keeps its capture.

HC-SR04 narrows timeout to the catalogue's 25000..30000 us and trigger to 10 us.
It waits at least 62 kernel milliseconds after each completed attempt,
exceeding the datasheet's 60 ms even with millisecond truncation and task delays.
A failed/backwards clock stops the worker for lifecycle recovery; it cannot
authorize another trigger. Conversion uses rounded
`echo_us * 10 / 58` millimetres and admits 20..4000 mm. This assumes nominal
sound speed, with no temperature compensation or outlier filtering.
Multiple ultrasonic sensors still need acoustic scheduling/placement to avoid
crosstalk; separate software contexts cannot prove physical independence.

The instance publishes fresh kernel-millisecond timestamps, source index,
sequence, distance and raw echo width. Timeout/invalid/I/O records have zero
measurements and explicit status. Busy/cadence skips produce no sample or
sequence increment. Queue contention drops the attempted publication rather
than replaying old values. Faults report through the existing health API;
contention can undercount health events. The host fake deliberately returns a
nonzero width on failures to verify that callers clear it.

## PWM and electrical boundaries

Each of four C3 PWM channels has its own LEDC timer: 100..20000 Hz, 10-bit duty,
XTAL clock, fractional divider rounded up. Achieved frequency is quantized and
no greater than requested. Duty requests are permille 0..1000; intermediate
values round to hardware ticks. Zero/full duty use fixed GPIO low/high levels
to avoid the full-period register overflow. Frequency is fixed until close/open.
Updates do not promise glitch-free phase continuity.

Setup starts low. Cleanup reconnects a low GPIO latch before stopping the timer
and deleting resources; a failed deletion keeps ownership. An unlock failure
also attempts low output and reports an error. The cached duty is only the last
successful request, not measured actuator state. Once cleanup releases the pin,
it is high impedance: an external pull-down/driver enable is needed if an
actuator must stay disabled through reset, disconnection or released ownership.

All protocols share `c3pin`'s exposed GPIO0/1/3/4/5/6/7/10 ledger. USB, flash,
straps and kernel UART20/21 remain reserved. Digital input adds no pull or
interrupt/debounce service. Generic asynchronous GPIO event delivery remains
unimplemented; distance edge measurement is handled by RMT.

Classic HC-SR04 uses a 5 V supply/Echo signal. Confirm the actual module and use
appropriate Echo level shifting/division before connecting it to the C3 input.
Actuators require a suitable driver and supply; a GPIO PWM signal is not motor
power. Confirm wiring and record actual physical output acceptance separately.

## Verification and manual BARR-C review

Run the mandatory C gate. Required host suites include `gpiotest.c`, which tests distance bounds, cadence including delayed measurement, zeroed failures,
duplicate ownership, shared pin conflicts, partial setup, PWM command bounds,
zero-duty cleanup, independent registry instances, typed samples/health and
activation recovery. Host fixtures manually invoke steps; they do not emulate
RMT/LEDC registers, native scheduling or electrical behavior.

See [software/build evidence](../../qualification/esp32c3-gpio-20260928.json).
The ordinary board fixture inspects factories, descriptors and invalid arguments;
it does not establish physical HC-SR04/PWM acceptance.

Manual review checked module boundaries, initialized state, borrowed lifetimes,
owner identity, retained failures, bounds, fixed-width unsigned operations,
aligned copied commands/configs, finite loops and task-context synchronization.
The distance multiply is bounded by 30000 us; duty arithmetic widens before
multiplication; timer/channel casts are factory-bounded to 0..3. The clock
calculation widens before multiplying and validates the 18-bit divider field.
RMT MMIO is aligned/static/volatile and read only after taking software ownership.
No heap allocation, ISR, policy/deviation change or new C11 use was added.

Ordinary modules compile as C99. `c3pulse.c` and `c3pwm.c`, like the existing
I2C/SPI/pin adapters, require GNU99 solely to parse pinned external LL headers;
owned code uses C99 syntax. The exact translation units are checked in the build
helper. The two approved C11 files and 49 kernel compatibility exceptions remain
unchanged. A passing gate is not certification of all BARR-C requirements.

Final hardware acceptance must inspect RMT edge order and trigger width with
loopback/known pulses, 20..4000 mm distances, timeout/stuck-high behavior, noisy
input and repeated reconfiguration. Measure PWM frequency/duty/endpoints and
stop/reset behavior with a safe load, including independent channels. Exercise
concurrent I2C/SPI/UART/capture/PWM, kernel timing, stack/heap and pin conflicts.
The synthetic first-edge technique in particular needs physical confirmation;
the backend is compiled but not hardware-qualified.

Sources: [Espressif C3 technical reference](https://documentation.espressif.com/esp32-c3_technical_reference_manual_en.pdf),
[HC-SR04 datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf),
and the pinned SDK LL headers plus native kernel SYSTIMER implementation.

Before hardware acceptance, confirm modules, wiring, voltage conversion and
actuator supply/driver. Follow the [hardware guide](../../../CONTRIBUTING.md).


Current [acceptance status](../../qualification/README.md) and
[wiring procedure](../../../CONTRIBUTING.md). Keep dated results in the qualification records, separate from current usage.
