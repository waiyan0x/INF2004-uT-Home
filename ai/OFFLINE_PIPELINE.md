# Training without equipment

The offline pipeline now runs from sample generation through training,
evaluation and a C export. It never opens a serial port, changes the UI or
applies a configuration. The current model is a **simulation-only rehearsal**.
Real measurements, retraining and board acceptance are still needed before use.

The model answers one narrow question: will a proposed reading or display
interval meet the scenario's freshness goal? It then offers the longest passing
candidate. Device identity, protocol, wiring and driver support stay in the
existing deterministic discovery/catalogue flow.

## Run it

Run commands from `Code`. This workspace already has an isolated environment
at `build/ai-venv`. For a fresh setup:

```powershell
python -m venv build/ai-venv
& build/ai-venv/Scripts/python.exe -m pip install -r ai/training-requirements.txt -r tools/c_standard/requirements.txt
$env:BARR_CLANG_FORMAT = Join-Path $PWD 'build/ai-venv/Lib/site-packages/clang_format/data/bin/clang-format.exe'
gcc --version
```

The full rehearsal uses a new output folder and takes less than a minute on the
current development computer. Existing files/folders are preserved:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.offline demo build/my-offline-run
& build/ai-venv/Scripts/python.exe -B -m unittest discover -s ai/tests -v
```

Outputs under `build/` are ignored by Git:

| File | Purpose |
| --- | --- |
| `runs.json` | Simulated observations, grouped by experiment and repetition. |
| `dataset.json` | Validated examples and their labels; rejected scenarios are listed separately. |
| `trained/model.json` | Small decision tree as plain JSON, with origin and training settings. |
| `trained/evaluation.json` | Separate train, validation and held-out test results, including simple-rule comparisons. |
| `trained/environment.json` | Python and training-library versions. |
| `trained/recommendation.json` | One explicitly experimental recommendation, possibly an abstention. |
| `trained/c/` | Generated C/header, formatting policy and host parity executable. |
| `trained/c-checks.json` | C standard findings, static data size and Python/C comparison results. |

You can also run stages separately:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.offline simulate build/runs.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline dataset build/runs.json build/dataset.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline train build/runs.json build/dataset.json build/trained
& build/ai-venv/Scripts/python.exe -B -m ai.offline recommend build/trained/model.json build/ai-timing/example-t1-20261013-final.json build/recommendation.json --allow-simulated
```

The last command may abstain if the experiment differs from the training domain.
The flag explicitly permits offline use of simulated models. Even with the flag,
the result says `deployment_allowed: false`; the model is not wired into Apply.
Use `python -m ai.timing` to prepare a new experiment when its reviewed sources
change. Keep old artifacts with their original source revision.

## What the simulation means

`simulator-v1` is a small test generator, not an electrical or scheduler model
of an ESP32. It creates sensor-age observations from interval/phase plus an
assumed workload delay and jitter. Display delay uses the source interval plus
refresh interval and assumed delay. Very frequent work under the toy load rule
produces stale observations. The equations are visible in `ai/offline.py`.

These assumptions exercise successful, failed and competing choices. They do
not prove timing, power savings, sensor behaviour or detection accuracy. Each
repetition has a new simulated session, and trial order is shuffled with a fixed
seed. Different goals on the same profile share a family; identical generated
profiles also share a family even across seeds. `--families` requests case pairs;
identical profile families are merged rather than split across evaluation sets.

The default rehearsal produced 48 scenarios in 20 distinct families, 576 runs
and 192 labelled candidate intervals. Twelve families were used for training,
four for validation and four for the held-out test. Hyperparameters were fixed
before looking at test results; no model search or test-set tuning was performed.

## Run and label contracts

The namespaced contracts `uthome.timing-run`, `uthome.timing-runs`,
`uthome.timing-dataset` and `uthome.timing-tree` use version **0.1.0**. They are
separate from the historical prototype AI 0.2.0 API. The run's structure is
defined in [timing-run.schema.json](contracts/timing-run.schema.json); cross-record
rules are enforced by `ai/records.py`, and model structure by `ai/model.py`.

Each run references the exact experiment, interval and repetition, and contains
elapsed time, board uptime, session/profile correlation, metric, quality and a
cumulative **fault-only** counter. Do not substitute the total health-event count:
normal reports are not faults. Observations must cover the planned window, begin
at elapsed zero, be ordered and stay within twice the declared sampling cadence.
Detected restarts, changed sessions/profiles, decreasing fault counters and
incomplete windows invalidate the run. This checks supplied records; it cannot
detect a reset that a recorder fails to observe or report.

A complete run passes only if post-warmup observations are fresh, its metric
stays within the goal, and the fault counter never increases during the run.
Every planned repetition must pass for that interval's label to be positive.
Missing or unusable repetitions exclude the whole scenario, preventing an
incomplete set of alternatives from being labelled as a best choice. Complete
failures are retained as negative examples. Queue drops are not treated as
faults automatically because unconsumed queues can discard old readings by design.

Hardware records additionally require a target manifest, firmware hash, identity
evidence hash and an appropriate observation method. These are declared evidence
references, not cryptographic proof that a run happened. A future recorder must
retain the referenced originals and establish the actual profile/build match.
Synthetic and hardware records cannot be mixed in one dataset. The equipment-free
work implements file replay/validation; live serial collection remains separate.

## Features and model boundaries

The model has six ordered integer inputs, available before the proposed run:

1. Execution kind: periodic reading (0) or queue consumer/display (1).
2. Proposed interval in milliseconds.
3. Requested freshness/delay limit in milliseconds.
4. Number of enabled device tasks.
5. Slowest configured source period, or zero for a producer.
6. Configured operation timeout in microseconds; this is a limit, not measured work time.

Actual outcomes, labels, scenario names, device IDs and descriptor defaults are
not features. Identity remains available separately to check source versions and
map the result to the correct instance. A display whose source cadence is unknown
(including the current stream descriptor) is unsupported by this first contract.
Radar stream and actuator intervals are not tuning targets.

Training uses a pinned
[scikit-learn decision tree](https://scikit-learn.org/1.8/modules/generated/sklearn.tree.DecisionTreeClassifier.html):
depth at most four, at least three examples per leaf, fixed seed 42. A candidate
needs a leaf pass fraction of at least 900/1000. This is an empirical fraction,
**not calibrated confidence**. Values outside the training ranges or unseen
catalogue/build contexts abstain. Range checks do not prove every combination
inside the ranges was adequately tested.

Family groups remain intact in the 60/20/20 split (rounded by whole families).
At least ten groups and both pass/fail training labels are required. The report
compares the tree, current setting, descriptor default, majority classifier and
a simple rule (`interval + source_period <= goal`). It reports bad choices,
abstentions and agreement with the longest tested passing interval. Training
always regenerates the dataset from source runs to reject edited labels/features.

The model is stored as bounded plain JSON, not a pickle. Prediction on the PC
uses the integer representation exported to C. Recommendations change one field
and pass shared whole-profile validation; no model result bypasses admission.

## Results and remaining work

In the recorded 2026-10-13 rehearsal, the tree had 21 nodes. On ten held-out
scenarios it chose the longest tested passing interval in eight cases and missed
the goal once. The simple rule achieved nine optimal choices and also missed the
goal once. **This gives no reason to replace the simple rule with this model.**
The 37/40 correct candidate classifications describe this toy simulation only.

The C export matched Python on 1,248 vectors, including dataset inputs, split/range
boundaries and random vectors. An ESP32-C3 object compile reported 584 bytes of
text/read-only data, zero writable static data, and zero local stack bytes for
the optimized leaf function. These exclude caller/integration overhead and are
not a linked firmware footprint or an on-board latency measurement.

With the equipment available, the remaining steps are physical discovery
acceptance, actual build/profile-correlated capture, measured dataset collection,
retraining and comparison, then firmware integration/resource measurements only
if the model improves on rules. Keep scenario families together and reserve
untouched real test runs. The software pipeline and simulator cannot supply
those missing measurements.

## Optional C3 compile without a board

Use the installed RISC-V compiler path explicitly:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.offline cross-check build/my-offline-run/trained/c --compiler 'C:/Users/kkak4/esp/uthome-c3-tools/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin/riscv32-esp-elf-gcc.exe' --output build/my-offline-run/trained/c3-object-check.json
```

This compiles a standalone object with C99 and warnings as errors. It neither
links the project firmware nor accesses hardware.

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
