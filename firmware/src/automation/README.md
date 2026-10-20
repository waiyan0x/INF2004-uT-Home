# Generic rules and output routing

Checkpoint 7 implements bounded sensor-to-output rules for the broader platform.
It uses common sample types, the existing guarded source cache and the existing
PWM component. The generic runtime and HAL gain no device-name branches.
Physical output acceptance remains open; offline AI work is a separate pipeline.

Only two C modules are added: `rulecalc` evaluates copied typed samples;
`cfgrule`, under `config/`, independently admits rules. The existing binder
copies one rule into each output instance and calls `component_write` with a
`pwminst_command_t`. No interpreter, action queue, separate rule task, output
arbitration framework or dynamic rule allocation is introduced.

## Configuration and timing

Node schema **2.1.0** adds an optional `rules` list, with at most four entries.
Existing 2.0.0 sensor/display profiles retain their meaning. Any PWM instance or
rules field requires 2.1.0. Each enabled output needs exactly one rule; duplicate
writers, absent/wrong-type sources and disabled sources for enabled outputs are
rejected. Disabled outputs still reserve their pins. All devices share the
existing eight-instance/four-active-worker/16 KiB peripheral-stack limits.

See [the complete distance/output example](../../../configs/nodes/distance-controlled-output.json):

```json
{
  "source": "front_range",
  "target": "output.main",
  "metric": "distance_mm",
  "comparison": "below",
  "threshold": 400,
  "hysteresis": 50,
  "cooldown_ms": 200,
  "max_age_ms": 500,
  "duty_permille": 500
}
```

This requests 50% duty below 400 mm. Once active, it remains active while below
450 mm; equality deactivates when cooldown permits. `above` uses the mirrored
lower hysteresis boundary. Initial activation is strictly above/below threshold;
equality with zero hysteresis stays off. Cooldown is the minimum time between
ordinary on/off changes, including reactivation after a fault.

| Metric | Input type | Threshold and hysteresis units |
|---|---|---|
| `distance_mm` | distance sample | Millimetres |
| `temperature_centi_c` | environment sample | Hundredths of degrees Celsius; signed threshold |
| `pressure_pa` | environment sample | Pascals |
| `target_count` | target report | Number of present targets, 0..3 |

Configuration thresholds are signed integers within +/-1,000,000,000; hysteresis
is 0..1,000,000. Cooldown is 0..60,000 ms; maximum age is 1..60,000 ms. Temperature
samples are promoted and multiplied by 100 for comparison without integer
truncation; configuration units do not imply sensor accuracy.

The `pwm_output` descriptor accepts a GPIO, frequency 100..20,000 Hz, periodic
evaluation 20..1000 ms, and active duty 1..1000 permille. Inactive duty is always
zero. Its `timeout_ms: 1` is the existing descriptor execution field for a
bounded register write, not measured physical latency. Actual reaction time
includes the output period and scheduling jitter. Example GPIO1 is an illustrative
assignment; actual actuator wiring/driver stage and supply need review at 8.

## Source freshness and ownership

Rules select an exact source instance, even when several producers share a FIFO.
They copy the cache entry under the existing bounded shared guard, then release
the guard before evaluation or output I/O. OLED, rules and USB observation do not
steal queued samples. Source freshness requires both the driver's configured
stale limit and the rule's maximum age; equality at either limit is stale.

Missing, stale, wrong-type, invalid/non-finite, future-timestamp or backward-clock
data, and cache contention, request **zero on the next output step**, bypassing
cooldown. Each unavailable step restarts the reactivation holdoff; fresh input
must then satisfy cooldown and condition. Startup begins at zero. Ordinary writes
are skipped when the commanded duty has not changed.

A failed command triggers one best-effort zero command, reports device I/O
failure and returns a worker error. This cannot guarantee a physical zero if
the hardware/write path has failed. Cleanup uses the existing checked zero/close
path and retains ownership for explicit recovery if it fails. Whole-plan
replacement/rollback resets rule state and reconstructs only admitted settings.
Reboot may restore a saved profile, whose output still starts at zero until its
source and rule allow activation. Physical feedback, emergency-stop electronics,
load-specific motor/relay drivers and measured timing are outside this software
proof. Raw ESP GPIO is a control signal, not actuator load power.

## Manual BARR-C review

Reviewed the changed wire/pump/capability APIs, integer parser, admission,
binding, source cache access, rule evaluator, C3 callback wiring and C fixtures
against every manual checklist category in `C_CODING_STANDARD.md`:

- C99, paired headers/public prefixes, private prototypes before use, static
  helpers and initialized fixed-width state. Existing C11/GNU99 boundaries and
  all approved exceptions are unchanged. Binding helpers were split to keep
  selection, setup and copied output settings separate.
- Integer tokens are bounded before cJSON conversion. Signed thresholds are
  checked before casts; pin/queue/source/metric indices are bounded before
  shifts, indexing or narrowing. Double comparison avoids threshold/hysteresis
  integer overflow, with finite checks and explicit temperature scaling.
- All wire fields use byte encoding, not struct layout. Capability strings and
  counts are bounded; read-only commands preserve controller retry state.
- No borrowed JSON/sample pointers escape; configuration and observations are
  copied. Rule state belongs to one output worker, resource transitions to the
  controller. Shared access uses the existing nonblocking guard; no new ISR or
  volatile/atomic contract is introduced.
- Loops and parser depth are bounded; no new heap owner. Every I/O return affects
  the result or has a documented accounting-only discard. Failed setup, writes,
  close and rollback retain the correct owner and are covered by fault tests.
- Function comments, constants, grouping, braces and formatter output were
  reviewed; assertions remain enabled in host fixtures. Test fakes share the
  runtime clock when testing sample freshness.

This review and the gate are software evidence, not certification of all BARR-C
rules or hardware behavior. See [fresh build/test and memory evidence](../../qualification/esp32c3-ui-rules-20260928.json).

## Verified software and memory

All 151 host tests pass (54 standard, 26 foundation, 18 runtime, 53 target),
including extended C rule/binding/failure fixtures and nine UI workflow tests.
The unchanged mandatory gate covers 180 owned C/H files with no findings or
tool/policy errors. Both real target modes pass, with 122 inputs and 51 owned
compiler profiles. Offline Edge review/error/rule/mobile checks also pass.

The application binder occupies 12,800 bytes, previously
12,640; the transport pump
occupies 10,456 bytes. The same six application/cache objects total
51,088 bytes, previously 50,920.
Display GPIO handles remain 16 bytes. These are static
ELF measurements, not available heap or measured task-stack peaks. Existing
runtime/kernel objects and stacks are additional; AI capacity needs checkpoint
8 measurements. The application partition layout remains byte-for-byte unchanged.
