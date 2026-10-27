# Optional fan, LED and active-buzzer code

These additions provide new outputs using the current digital-output contract.
They do not modify the existing radar, distance, temperature, OLED, PWM,
configuration, runtime or board code. No pin is assigned, no worker starts,
and no deployed profile is replaced. The older `ug_` starter API is not used.

## Implemented files

| Module | Function |
| --- | --- |
| `firmware/drivers/relayctl/relayctl.c/h` | Steady ON/OFF relay commands, active-high/low polarity, minimum switching interval, immediate fault OFF and latched write failures |
| `firmware/src/services/fanctrl.c/h` | Existing `rulecalc` temperature/hysteresis/freshness logic connected to the relay |
| `firmware/drivers/signalout/signalout.c/h` | Separate instances for LED or active buzzer; steady output or timed pulses, mute, shutdown and fault handling |
| `tests/unit/outputs_main.c/h` | Executable examples and fault/timing tests using the actual shared rule evaluator and pin pool |
| `tools/test_outputs.py` | Strict C99 build, host tests and mandatory repository standard check |

No new temperature sensor, proximity sensor, ADC or display is introduced.
The potentiometer requires a separate ADC task and is deferred. A passive buzzer
needs a tone/PWM implementation and must not be attached to this digital-only
buzzer implementation expecting it to produce a sustained tone.

## Run on your computer

From the folder containing `firmware`:

```powershell
python tools/test_outputs.py
```

Requires GCC (or a compatible compiler selected by `CC`), Python 3 and the
project's pinned clang-format 20.1.8. The runner discovers the workspace-local
formatter in `build/dev-tools/clang_format/data/bin/clang-format.exe` if present;
otherwise install the existing development requirements or set
`BARR_CLANG_FORMAT` to your pinned executable. Build files stay temporary.

The demo prints temperature-driven ON/OFF and stale-data shutdown. It simulates
electrical outputs; it does not flash the board or drive connected hardware.

The runner stops on a failed standard gate. Keep assertions and warnings-as-errors
enabled; functional success cannot replace a passing policy gate. Historical
starter naming/build failures are in the [completed-work log](COMPLETED_WORK_LOG.md).

## What the functions do

`fanctrl_open` accepts a borrowed `digiout_t` and the existing
`rulecalc_config_t`. The example uses an above-temperature threshold of 3000
(30 C), hysteresis 200 (2 C), and full-scale command 1000. These mean:

- A fresh temperature strictly above 30 C requests ON.
- Once active, temperature at or below 28 C requests OFF.
- Normal relay changes obey its configured minimum interval; startup is OFF
  for at least that interval.
- Missing, stale, wrong-source, wrong-type, non-finite or fault samples request
  OFF on the next step, bypassing the switching interval. Clock regression does
  the same and returns an error. OFF is a model-fan demo policy, not a policy
  suitable for every cooling or safety application.
- `relay.b_on` records a successful command, not proof of fan rotation. If
  `relay.b_fault` is set, actual output state is unverified. Return codes must be
  handled by the caller, including reporting health errors.

Pass a coherent sample snapshot to `fanctrl_step`, rather than reading the
sensor a second time. This preserves the original sensor's single owner and
does not consume telemetry queue items. Keep the same monotonic clock origin.

Each `signalout_t` has its own line and configuration. Set `off_ms=0` for a
steady LED. For an active buzzer, an example is `on_ms=100, off_ms=900`.
`signalout_step(signal, enabled, muted, now_ms)` computes the current phase
without sleeping. Repeated enabled calls do not restart the pattern; unmuting
starts a new ON phase. Late calls skip missed edges; they do not emit a burst
of catch-up beeps. The owner must call often enough for the desired timing.
Use an authoritative current fault flag, not merely a nonzero historical health
counter, to enable a fault alarm. Muting the buzzer should not mute the LED.

After a write error, both output modules attempt OFF, latch the fault and
prevent reactivation until successful close/reopen. Failed close retains
ownership so cleanup can retry. Even a successful command cannot prove the
physical relay or fan worked. Scheduling stalls can leave an output in its last
state: the modules are not an independent hardware watchdog.

## Integration steps for the team

These modules are callable code, not newly supported JSON device types. The
production registry/admission/binder still supports only its existing types.
Do not rename `pwm_output` to a relay or send PWM to a mechanical relay.

1. Confirm controller model, available pin allocation, relay trigger polarity,
   buzzer type and all electrical ratings. Review the entire active profile.
2. Add reviewed relay/indicator descriptors and compiled registry/binder entries
   together with independent host and target admission checks. The proposal in
   `configs/examples/optional-outputs.proposed.json` is planning data only and is
   not accepted by the node validator. No target capability is advertised here.
3. In the existing lifecycle, claim the line through the existing shared C3 pin
   owner (`c3pin_open`), with the inactive initial level. Then construct the
   borrowed `digiout_t` using `c3pin_set` and the retained output handle. Do not
   bypass that layer with raw SDK GPIO calls or a separate ownership pool.
4. Register the optional worker in the resource plan. Keep each instance's
   state separate and serialize open/step/close. Stop the worker first, call
   the module's close, and only after success call `c3pin_close`. Keep handles
   when either cleanup fails. If open fails with `b_open=true`, retry cleanup.
5. Fan worker: copy the existing temperature observation through the approved
   shared-cache interface, then call `fanctrl_step`. Indicator worker: call
   `signalout_step` with the selected status and mute flag. Check every result.
6. Add JSON/source mapping, resource rejection and rollback tests, then build
   the real target before hardware testing. This change does not alter target
   source lists, target ABI, adapters or existing board tests.

The current four-active-worker limit still applies. Start with temperature +
relay/fan + OLED (three workers under the existing one-worker-per-device model).
One indicator can be a fourth. Do not activate both an LED and buzzer as extra
workers without a reviewed resource change or an explicitly designed shared
indicator worker. The latter is not implemented here.

## Electrical acceptance

- Power the fan from its rated 5 V supply, not a GPIO. Use the relay contacts to
  switch the low-voltage fan circuit; no household mains demo is involved.
- Verify the module's input is compatible with 3.3 V and establish active-high
  or active-low mode. Software polarity does not fix an incompatible voltage.
- Verify relay OFF through reset/boot using an appropriate hardware bias; the
  startup software runs only after boot and cannot guarantee earlier levels.
- Fit a current-limiting resistor for each LED. Check the active buzzer's current
  and voltage; use a suitable driver stage if GPIO cannot supply it.
- Start with unloaded logic tests, then the fan. Verify power capacity, physical
  OFF, switching interval, sensor disconnection, task cancellation and recovery.
- Choose a minimum relay interval suitable for the load. The test's one-second
  interval is not an air-conditioner/compressor operating recommendation.

## Verification and manual review

New code is C99, uses paired module headers and named callbacks through
`digiout`, and does not change public shared types. No compatibility deviations
or checker/formatter policy changes were made. Manual review covers:

| BARR-C area | Review result |
| --- | --- |
| 1: language/ABI/concurrency | No new SDK/kernel calls, heap, ISR or concurrency primitive; single-owner contract is explicit |
| 2–3: comments/format | Contracts state polarity, timing, failure/ownership and hardware limits; pinned formatter applied |
| 4: modules | Relay, indication and temperature composition remain separate; existing rule evaluator is reused |
| 5: types | Bounded timings, 64-bit elapsed time, clock-regression handling, validated finite temperature, no serialized structs |
| 6: functions | Prefixes match modules; private helpers are static; no blocking loops |
| 7: state/lifetime | Zero-init required; borrowed handles retained until successful cleanup; write failures latch |
| 8: control/errors | Checked writes and OFF retry, preserved error status, no assertions disabled |

Tests cover both relay polarities, startup holdoff, normal minimum interval,
forced OFF, failed setup/write/cleanup, fault latch/reopen, clock regression,
indicator phase/mute/steady output, temperature hysteresis, stale/fault/NaN/
wrong-source/future data, invalid configuration without I/O, and pin-pool
collision/release protection. Hardware timing, RAM/stack peaks and production
profile integration remain unverified. The pin-pool test demonstrates the
existing ownership mechanism; it does not claim these borrowed-line modules
allocate or independently police physical pins.
