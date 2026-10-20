# AI timing pipeline

The current model is an **experimental decision tree trained on simulated data**.
It predicts whether a proposed reading/display interval meets a freshness goal,
then selects the longest passing candidate. It is not linked into the firmware
or the UI Apply flow. Device identity, protocol and wiring stay in the reviewed
[discovery/catalogue flow](../docs/README.md).

## Run the offline pipeline

From the repository root, with GCC on PATH (PowerShell):

```powershell
python -m venv build/ai-venv
& build/ai-venv/Scripts/python.exe -m pip install -r ai/training-requirements.txt -r tools/c_standard/requirements.txt
$env:BARR_CLANG_FORMAT = Join-Path $PWD 'build/ai-venv/Lib/site-packages/clang_format/data/bin/clang-format.exe'
gcc --version
& build/ai-venv/Scripts/python.exe -B -m ai.offline demo build/my-offline-run
& build/ai-venv/Scripts/python.exe -B -m unittest discover -s ai/tests -v
```

Reuse an existing environment instead of recreating it. GCC and clang-format
20.1.8 are required for C export checks; training pins scikit-learn 1.8.0.
Use a fresh output path: artifacts are never overwritten and `build/` is ignored
by Git. These commands do not open USB, apply profiles or flash firmware.

| Output | Purpose |
|---|---|
| `runs.json` / `dataset.json` | Simulated observations and validated labels |
| `trained/model.json` | Decision tree, feature bounds, origin and training metadata |
| `trained/evaluation.json` | Separate training/validation/test results and rule comparisons |
| `trained/environment.json` | Python and package versions |
| `trained/recommendation.json` | Experimental candidate or explicit abstention |
| `trained/c/` / `trained/c-checks.json` | Generated C/header, host executable and parity evidence |

Individual stages are also available:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.offline simulate build/runs.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline dataset build/runs.json build/dataset.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline train build/runs.json build/dataset.json build/trained
```

## Prepare an experiment

Choose one enabled device instance and a freshness/display-delay goal. Periodic
producers and displays with known source cadence are supported; radar streams
and actuator tuning are outside this model. Rule-controlled outputs must already
be disabled in the supplied profile. Every trial changes only the selected
interval and must pass whole-profile admission.

```powershell
New-Item -ItemType Directory -Force build/ai-timing | Out-Null
& build/ai-venv/Scripts/python.exe -B -m ai.timing prepare --profile configs/nodes/two-environment-sensors.json --build-manifest configs/builds/host-reference.json --scenario ai/examples/timing-scenario.json --output build/ai-timing/example.json
& build/ai-venv/Scripts/python.exe -B -m ai.timing validate build/ai-timing/example.json
& build/ai-venv/Scripts/python.exe -B -m ai.timing export build/ai-timing/example.json --interval-ms 1000 --output build/ai-timing/room-1-1000ms.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline recommend build/trained/model.json build/ai-timing/example.json build/recommendation.json --allow-simulated
```

The example is a host fixture, not a wiring instruction. It compares 200, 500,
1000 and 2000 ms for `room_1`, leaving `room_2` unchanged. The 1500 ms goal is
illustrative. Exported profiles still need connected capability review and Apply.

Edit [the example scenario](examples/timing-scenario.json):

| Field | Meaning |
|---|---|
| `scenario_id`, `family_id` | Unique experiment and shared group for related conditions |
| `instance_id`, `conditions` | Exact device ID and what remains fixed |
| `goal.metric`, `goal.limit_ms` | Observed sample age or visible update delay and acceptable limit |
| `candidate_intervals_ms` | Up to eight distinct intervals; current/default settings are always included |
| `warmup_ms`, `duration_ms` | Settling time, then at least ten periods of the slowest candidate |
| `repetitions` | Two to ten runs per interval |

Plans contain no measurements or labels. They bind profile, build, catalogue and
implementation hashes; validation regenerates the plan. Preserve old artifacts
with their source revision and prepare a new plan when reviewed inputs change.
Hashes use canonical JSON content, not file whitespace, and are not signatures.

## Run records and labels

The current [contracts](contracts/README.md) use version 0.1.0. Each run identifies
its experiment, interval, repetition, session, profile and sampling cadence.
Observations include elapsed time, uptime, metric, quality and a **fault-only**
cumulative counter. Total health events and normal queue overwrites are not
automatically faults.

- Cover the full planned window from elapsed zero, in order, with gaps no larger
  than twice the stated sampling cadence.
- Detected resets, changed profiles/sessions, decreasing counters and incomplete
  repetitions exclude the scenario. A recorder must actually observe/report
  these events; validation cannot infer unrecorded interruptions.
- A positive interval label requires every repetition to stay fresh after warmup,
  meet the observed goal and add no faults during the entire run. Complete
  failures remain negative examples rather than being discarded.
- Hardware runs require actual build/profile correlation and retained identity
  evidence. Hash fields alone do not prove measurements happened. Never mix
  simulated and hardware records in one dataset.

File replay is implemented; live serial collection remains unfinished. Telemetry
provides observed ages and quality, not worst-case ages between polls, visible
display latency, energy use or every worker's execution time.

## Model and evaluation

Six integer inputs are available before the trial: execution kind, proposed
interval, goal, active task count, slowest source period and configured timeout.
Timeout is a limit, not measured execution time. Outcomes, labels, instance IDs
and descriptor defaults are excluded from features.

Training uses a [decision tree](https://scikit-learn.org/1.8/modules/generated/sklearn.tree.DecisionTreeClassifier.html)
with depth at most four, at least three examples per leaf and seed 42. A passing
candidate needs a leaf pass fraction of at least 900/1000; this is not calibrated
confidence. Unknown catalogue/build contexts and values outside training ranges
abstain. In-range values do not prove every combination was tested.

Related families stay together in approximately 60/20/20 train/validation/test
groups. At least ten families and both pass/fail training labels are required.
Compare the tree with current settings, descriptor defaults, majority prediction
and `interval + source_period <= goal`. Report goal violations, abstentions and
agreement with the longest **tested** passing interval. Labels/features are
regenerated from source runs before training; do not tune using the test results.

`simulator-v1` generates toy age/load/jitter behavior, not ESP scheduling or
electrical behavior. Identical profiles share a family, even across simulation
seeds. Simulation checks the workflow, not hardware accuracy or power savings.

## Results and remaining work

The [2026-10-13 record](qualification/offline-20261013.json) contains 48 scenarios,
20 families, 576 runs and 192 candidate examples. The 21-node tree chose the best
tested interval in 8/10 held-out scenarios; the simple rule achieved 9/10. Both
missed the goal once. There is no demonstrated reason to replace the rule.

C matched Python on 1,248 vectors. A standalone C3 compile reported 584 bytes of
code/read-only data and no writable static data; it was not a linked firmware
build or a board measurement. All recommendations remain non-deployable.

Next: physical discovery/acceptance, actual recording, measured training data,
untouched real evaluation, then integration/resource measurements if beneficial.
Keep the provisional 32 KiB model-flash / 16 KiB total-module-RAM / 10 ms observed loaded latency
targets as goals only; measured remaining resources determine feasibility.

## Next software step

Build a small host-side recorder before collecting real training data. This is
**planned, not implemented**; it can be developed with fake streams while the
equipment is unavailable. Reuse `tools/config_transfer/client.py`, its telemetry
decoder and `ai/records.py` rather than adding another protocol or UI framework.

1. Start with one periodic sensor and `observed_sample_age_ms`. Read an already
   reviewed/applied trial; do not automatically change or save profiles. Display
   latency needs an external measurement method and cannot be inferred from age.
2. Establish how the actual firmware image, active profile and confirmed device
   evidence will be tied to the run. Capabilities and an active revision/CRC
   alone do not prove an arbitrary firmware/profile SHA256. Refuse hardware
   labels when this correlation is unavailable; do not invent proof fields.
3. Record bounded samples, elapsed time, board uptime, quality and fault-only
   counter changes. Define the fault mapping explicitly; total events and normal
   queue overwrites are not fault counts. Use one serial owner, not a competing
   connection alongside the UI.
4. Preserve interrupted/incomplete results with reasons. Exercise disconnects,
   resets, stale/fault readings, profile changes and sampling gaps using fake
   transport and clock inputs. Pass complete runs through the existing run and
   dataset validation. Do not pad missing observations or relabel simulation.

Hardware capture is ready only when real build/profile correlation and the
selected metric can be demonstrated. Then collect grouped measurements, compare
against the simple rule on untouched data, and consider embedded integration.
Any necessary firmware instrumentation still requires BARR-C review and a target
build. UI additions need the user's review before implementation.

## Existing local model

On this workstation, the model is
`build/ai-offline-20261013/trained/model.json`; evaluation, C export and compile
checks are beside it. Runs and the dataset are in the parent output directory.
The model was reformatted for readability; its canonical JSON still matches the
recorded model hash. Preserve the dated evidence rather than replacing its
original byte counts/hashes after formatting.

These generated files and `build/ai-venv/` are ignored, not committed binaries.
A new chat on this workstation can reuse them; a fresh clone must regenerate
them using the commands above. All source hashes listed in the offline evidence
still matched on 18 October 2026. No new training or board testing was performed
for that handoff check.

## Optional C3 compile

Select the installed ESP RISC-V compiler explicitly:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.offline cross-check build/my-offline-run/trained/c --compiler 'C:/Users/kkak4/esp/uthome-c3-tools/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin/riscv32-esp-elf-gcc.exe' --output build/my-offline-run/trained/c3-object-check.json
```

This compiles only the module. No firmware is linked, applied or flashed.

## Legacy experiment

`uthome_ai/`, the old model/request/recommendation contracts and
`data/sources/first-prototype-baseline.json` retain AI 0.2.0 regression fixtures.
They must not generate new training data or deployment profiles. The historical
`python -m ai.uthome_ai.cli exchange` validates that exchange; it does not run
the current timing model.


## C review

The owned templates and host test use C99 and existing BARR-C policy without new
exceptions. `timtree` means timing tree; `tim_main` is its host parity entry point.
Generated sources also undergo the pinned format and lexical/module checks.

- Rules 1/5: six uint32 inputs and immutable integer tables; no floating point,
  casts, dynamic allocation, atomics, hardware qualifiers or callback ABI changes.
  Feature values stay below 2^24, so sklearn's float32 input conversion preserves
  integers; flooring a split threshold preserves integer comparisons.
- Rules 2/4/6: paired headers, module prefixes, documented borrowed pointers and
  outcomes; generated data is outside the deployed firmware. Functions are short.
- Rules 3/7/8: pinned formatting, initialized indices, explicit bounds, constant-left
  equality and bounded loops. Export validation proves acyclic child indices and
  depth <= 4; evaluation uses at most five node visits. No mutable global state.
- Host test uses explicit failure returns; disabling assertions cannot remove its
  checks. Inputs are generated bounded uint32 vectors. GCC uses C99, all requested
  warnings as errors and no diagnostic suppression. NULL/count checks are exercised.

The root BARR-C gate checked 194 owned C/H files with zero findings/tool errors.
Generated model C is checked explicitly even though `build/` is excluded from
the repository source walk. Passing checks do not certify all standard rules.
