# Preparing timing experiments

Checkpoint T1 prepares the tests we will use to build the training dataset.
It does not train a model or collect measurements yet. Device identification,
protocol, voltage, pins and driver support still come from the discovery flow
and reviewed catalogue.

The first question is: **how slowly can this device read or refresh while still
meeting the application's needs?** Start by comparing the current interval and
catalogue default with a few alternatives. A longer interval means fewer
scheduled operations; it does not by itself prove lower measured power usage.

## What the tool does

`python -B -m ai.timing` has three commands: `prepare`, `validate` and `export`.
It uses the existing node validator and the same Python dependencies as the
other AI tools. There is no additional service, UI or ML framework.

Each experiment targets an explicit instance, such as `room_1`. The tool looks
up its capabilities and interval limits in the catalogue, so two instances of
the same driver can be studied independently. There is no fixed model-name list.
It supports periodic sample producers and displays that consume readings.
UART streams do not expose a reading interval, and actuator tuning is outside
this first scope. Any rule-controlled outputs must already be disabled in the
input profile; the tool will not disable them for you.

Every trial changes just the chosen interval. The complete profile must still
pass admission, including other devices, pins, queues and resources. The tool
always includes the current interval and the descriptor default. These are
comparison settings, not labels claiming to be the best answer. It refuses
experiments with only one distinct interval.

## Try the offline example

Run these commands from the repository's `Code` directory. The example uses the
two-sensor **host fixture**, not today's wired bench. Its 1.5-second goal is an
illustration, not an agreed product requirement. The output directory is ignored
by Git; use a new filename for later revisions.

```powershell
New-Item -ItemType Directory -Force build/ai-timing | Out-Null
python -B -m ai.timing prepare --profile configs/nodes/two-environment-sensors.json --build-manifest configs/builds/host-reference.json --scenario ai/examples/timing-scenario.json --output build/ai-timing/example.json
python -B -m ai.timing validate build/ai-timing/example.json
python -B -m ai.timing export build/ai-timing/example.json --interval-ms 1000 --output build/ai-timing/room-1-1000ms.json
```

This prepares 200, 500, 1000 and 2000 ms trials for `room_1`, leaving `room_2`
unchanged. Export writes an ordinary compact node profile. Existing output files
are never overwritten. No command connects to USB, applies a profile or flashes
firmware. A bench profile must subsequently pass the existing connected
**Validate and review → Apply** flow, including actual firmware capabilities.

## Choose a useful scenario

Copy [the example](examples/timing-scenario.json) and edit these fields:

| Field | What to write |
| --- | --- |
| `scenario_id` | A unique name for this experiment. |
| `family_id` | A common name for related conditions. Keep all trials and repetitions from this family in the same future train/test split. |
| `instance_id` | The exact `device_id` in the supplied profile. |
| `conditions` | What stays fixed: connected devices, other workloads, scene or movement, and wiring. Record changed conditions as another scenario. |
| `goal.metric` | `observed_sample_age_ms` for readings; `visible_update_delay_ms` for a display. |
| `goal.limit_ms` | The oldest acceptable observed reading, or the longest acceptable visible delay. Agree this before testing. |
| `candidate_intervals_ms` | Up to eight distinct intervals to compare, inside the descriptor's limits. |
| `warmup_ms` | Time to allow the system to settle before recording. |
| `duration_ms` | Observation time per trial, excluding warmup; at least ten periods of the slowest interval, including both comparison settings. |
| `repetitions` | Two to ten runs per interval; three is a useful starting point. |

The versioned request schema is
[timing-scenario.schema.json](contracts/timing-scenario.schema.json). The tool
enforces bounds and integer values. Short warmup or observation settings are not
proof of a statistically sufficient experiment. When collecting later, vary the
trial order across repetitions and record that order, rather than always testing
the fastest setting first.

## What is recorded now

The `uthome.timing-experiment` contract, version `0.1.0`, stores the scenario,
original profile, selected build manifest, per-instance capability information,
planned resource use, and trial intervals with their profile hashes. It also
records versions and SHA-256 hashes for the board, used descriptors, discovery
policy, node/descriptor schemas and preparation/admission implementation. JSON hashes use
sorted keys and compact ASCII serialization; they describe content, not original
file formatting. They are integrity checks, not signatures or proof of hardware.

Validation regenerates the plan against the current reviewed sources. A change
to those sources or a changed trial causes rejection. Preserve the old plan and
its matching repository revision, then prepare a new plan. Do not patch old
records to make them look current.

Every plan explicitly says `stage: planned`, `measurement_origin: none`,
`identity_evidence: not_collected`, `actual_firmware_sha256: null` and
`training_label: null`. A supplied manifest, including one marked `target`, is
only planning input; this tool never verifies a connected build. Existing
discovery observations and earlier hardware tests are not silently reused as
evidence for a new experiment. This contract is separate from the historical AI
0.2.0 contract and is not yet a model input/output contract.

## Recording runs

The [offline pipeline](OFFLINE_PIPELINE.md) now implements run-file validation,
simulated data, training and C parity. Actual hardware capture remains pending.

Before measured collection, complete physical discovery acceptance and choose
the actual bench profile and application goal. No new teammate specification
is required to use the offline planner. We will need the following for real runs:

- Confirmed hardware identities, discovery/confirmation records, wiring and
  supported drivers; manual confirmation remains distinct from electronic ID.
- Actual firmware build hash and connected capabilities, exact applied-profile
  correlation, and the experiment/trial/repetition being measured.
- Timestamped observations with reset, reconnect, missing-sample and fault
  handling. A reset or configuration change ends a run; it must not be silently
  joined to the next session.
- Sampling cadence, run duration and trial order. Keep simulated captures in a
  separately marked test set, never in the measured training set.

Current telemetry exposes sample ages/quality, queue counts and health events.
It does not measure display-to-eye latency, energy consumption or every worker's
execution time. A maximum age seen in occasional snapshots is only the maximum
**observed** age; it does not prove the worst case between snapshots. Visible
display delay needs separate observation or instrumentation. Queue drops alone
also do not prove a fault: a readings queue with no consumer can intentionally
fill and discard older values.

Once comparable runs exist, select the longest **tested** interval that meets
the agreed goal and reliability checks across repetitions. If none qualifies or
evidence is incomplete, keep an explicit failure/insufficient-evidence result.
That selection is limited to the tested conditions and intervals. Compare with
both existing settings and simple rules before deciding whether a trained model
adds enough value. Do not train on the default interval as an assumed optimum,
or feed the selected answer/run outcome back into the model's input features.

## Checks

```powershell
python -B -m unittest discover -s ai/tests -p test_timing.py -v
```

This runs the timing-preparation tests with the base AI requirements. See the
offline guide for the full training/C suite. These are software checks, not
hardware qualification or measured model accuracy.
