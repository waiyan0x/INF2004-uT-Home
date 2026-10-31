# AI timing pipeline

The six-input simulated-data model and eighteen-input protocol model are
experimental. Both predict whether a candidate interval meets a freshness goal
and health criteria. Neither is enabled in ordinary firmware or the UI Apply
flow; the real-data studies failed their frozen deployment-benefit criteria.
The primary model has a separate target parity/timing diagnostic.
Device identity, protocol and wiring stay in the reviewed
[discovery/catalogue flow](../docs/README.md).

The [mandatory static-memory change](../firmware/README.md#mandatory-static-memory-change)
now takes priority before deployment qualification. Preserve both measured
studies as evidence for their original builds. Worker/resource lifecycle changes
require fresh timing evidence on the compliant firmware; the existing host
pipeline remains reusable. Budget embedded inference as immutable model data
plus fixed feature/work buffers and bounded stack use. No model is deployed.

<a id="static-memory-implementation-checkpoint"></a>

## Static execution contract

Follow the [overall delivery plan](../README.md#overall-delivery-plan) for system
completion. Preserve the working fixed parser, permanent workers and USB
measurements. This guide owns AI contracts and evaluation gates; dated results
belong in [qualification](../firmware/qualification/README.md) and the
[completed-work log](../docs/COMPLETED_WORK_LOG.md).

The user also requires Wi-Fi and BLE on the ESP. Resolve the
[wireless integration gate](../firmware/README.md#on-device-wireless-integration-gate)
before freezing the next training baseline: radio/service tasks, traffic and
reconnection can change timing and available resources. Count infrastructure
reservations separately; `active_tasks` continues to mean enabled peripheral
workloads, not every kernel task. Review whether the eighteen-input contract
adequately represents the supported service/load conditions. Version any required
feature change, calibrate only measurements available at inference time, and
evaluate on held-out sessions and supported device types. Until validated,
unrepresented radio/load contexts must receive no AI advice; normal deterministic
configuration remains available. No wireless-aware model is currently trained.

The collector, execution-context sidecar, fixed measurements and eighteen-input
exporter are implemented. The USB static core has scoped acceptance; on-device
wireless integration and a deployable model remain open. Current status is in
the [root guide](../README.md#current-status); completed study counts and build
results are in the [history log](../docs/COMPLETED_WORK_LOG.md) and dated JSON.

The execution sidecar is separate from historical experiment/run contracts.
`active_tasks` still means bound enabled workloads. Idle workers reserve their
entire stacks; the implemented 4 x 8 KiB reservation is distinct from active
requested stack budgets. Bind new measurements to the exact new image.
Missing allocation/object counters are `null`, not zero. A qualification record
needs the exact running ELF, twenty Apply cycles, all required checks and retained
evidence whose hashes are verified before the collector opens serial. These
checks validate a reviewed record; they do not manufacture a memory audit.

New `ai.protocol_capture` CLI calls require `--execution-context` and
`--qualification-folder`. The collector rechecks the running ELF before Apply
and retains `static-bindings.json` alongside existing journals. Its legacy Python
entry point remains readable for historical tests; it does not grant new static
qualification. Freeze the sidecars together with the next study and its sources.
Do not train a release from folders lacking those bindings. Use
`python -B -m ai.static_readiness` to list known blockers (exit 2 while blocked);
an empty source scan alone is never evidence of compliance.

`workcost` is now linked through `workmeter` and sampled by command 9 in the
qualified USB measurement image. Four records occupy 176 bytes of caller-owned
state, separate from AI request storage, using the existing shared guard. See
[runtime handoff](../firmware/src/runtime/README.md#workload-measurement-handoff).
The study records step/transfer wall time, declared transfer bytes, counters and
stack observations alongside the existing eighteen configuration inputs. These
are auxiliary measurements, not measured CPU utilization or electrical wire
time. Missing observations remain unavailable; no new learned feature has been
introduced or tuned from the held-out results.

To export an existing experimental protocol model without installing it:

```powershell
python -B -m ai.protocol_export --model build/protocol-study-20261026-r3/trained/model.json --output build/protocol-export-new --cross-compiler PATH_TO_PINNED_RISCV_GCC
```

Set `BARR_CLANG_FORMAT` to pinned 20.1.8 and put host GCC on PATH. The destination
must not exist. The export retains `deployment_allowed: false`; its C classifier
does not itself enforce catalogue/build compatibility, candidate selection or
profile revision. The fixed adviser implements identity/state checks; its transport and release
model binding still need integration before normal reviewed Apply. This seven-node object measures 400 bytes code/read-only
data, zero writable globals and compiler-reported zero stack bytes for the
classifier alone; these are standalone object results, not whole-request or
linked-firmware measurements. The largest accepted 31-node tree has 764 bytes
of tables. An unseen supported peripheral is still required before claiming
cross-device accuracy.

## AI plan for the agreed static-memory design

### Implemented bounded controller preparation

`firmware/src/services/adviser.c/.h` now provides a portable, caller-owned
432-byte state machine with at most four copied eighteen-input candidates.
It evaluates one candidate per step and returns the longest passing interval.
Every step and reply checks the profile CRC/revision, session, build and execution
context; stale identity, reversed clocks, a 100 ms scheduling deadline and a
30-second result lifetime invalidate advice. Unsupported/unqualified contexts
never invoke the predictor. The separate 10 ms inference-time goal remains to
be measured on the target.

Manual BARR-C review: paired headers and named callbacks preserve C99; feature
arrays are bounded, intervals and predictor results are checked, and copied
requests prevent caller mutation during inference. State has one serialized
owner and no allocation, kernel object, Apply side effect or UI dependency. The
model descriptor must remain immutable after initialization. All current model
exports remain unqualified for deployment. A release must explicitly bind a
qualified measurement context to its combined image; adding the model changes
the ELF hash and cannot be accepted merely by filling in the running build ID.
The paired `adfeat` module reconstructs all eighteen inputs from an independently
admitted `cfgplan_t`. Five host tests compare actual target parsing plus C feature
construction against Python across synthetic workload variants, priority/goal
changes and fractional-rate rounding. Invalid targets/goals clear the output.
The supported GPIO sensor is admitted normally and receives no AI advice.
Declared UART traffic uses the exact frame shape already enforced by the compiled
descriptor; baud rate is never treated as continuous traffic. Rules and unknown
execution shapes abstain. Driver identity is used only to interpret supported
metadata, never included as a learned input.

`adfeat` has no writable global state. Standalone object sizes do not establish
whole-request stack usage; retain linked and measured evidence separately.
Manual review: at most eight devices are visited, divisors are nonzero and bounded,
and rate sums cannot exceed eight million before the versioned bounds check.
Only complete vectors are published. The caller must retain a stable admitted
plan and provide a nonaliasing output; the function is not another admission or
model-compatibility authority. A versioned advice endpoint, release-model binding
and reviewed Desk presentation remain outstanding.

### Agreed design context

The following Desk proposal awaits owner review before implementation. Add one collapsed **Timing advice** card in
the existing Profile tools column, below discovery, using the existing card and
field components. It would offer an enabled periodic producer and a freshness
goal in milliseconds, followed by **Check timing**. Advice refers to the current
reviewed draft and connected board, not to arbitrary supplied feature arrays.

| Result | Proposed presentation and action |
|---|---|
| Qualified recommendation | Show current and suggested intervals plus the freshness goal. **Use in draft** changes only that field and returns through existing **Validate and review** and **Apply & save**. |
| Unsupported device/load or no qualified model | Explain that timing advice is unavailable for this configuration. Existing editing, validation and Apply stay available. |
| No passing candidate | State that the evaluated intervals did not produce a recommendation; retain the draft. |
| Busy, expired or changed draft/connection | Show progress or request a fresh check; discard the old recommendation. |

Model identity and evaluation scope belong in an expandable detail, without
presenting a training probability as a guarantee. No larger local/API model is
required for this flow. If explanatory AI is added later, it must not override
driver admission, qualification or reviewed Apply. UI implementation and its
loaded end-to-end acceptance remain deferred pending the owner's review.

The tribunal design specifies four permanent 8 KiB workers, startup locks,
fixed parsing and the unchanged 8192-byte input limit. These are implemented
for the USB core. The 32 KiB stack reservation is not spare AI memory. Discovery
keeps eight I2C result slots and a 64-byte UART read buffer under exclusive ownership.
The supplied document is design context, not automatic authorization to edit
the tribunal submission or a qualification of the integrated wireless system.

### Intended behavior and ownership

The first AI release remains a timing adviser: predict whether an admitted
periodic sensor interval meets a freshness goal under the supported I2C/SPI/UART
workload, then choose the longest passing interval from a small fixed candidate
list. This reduces requested polling frequency; energy savings require separate
measurement. Discovery, driver identity, wiring, memory admission and recovery
stay deterministic. A model cannot make an over-capacity profile admissible.

Proposed flow: reviewed device/profile -> normal admission -> supported-context
check -> bounded model inference -> advice or abstention -> user review -> normal
target validation and Apply. Advice must carry its profile revision/hash and
model/build identity; changing the profile or firmware invalidates stale advice.
Inferred timing is never applied automatically or used to resize queues, stacks,
UART storage or parser capacity. Unsupported devices retain normal configuration
through their compiled drivers, with no AI advice.

These are subsystem contracts within the overall implementation effort; original
member labels identify the existing components, not external work to wait for.

| Subsystem / original contribution | Contract to implement and verify |
|---|---|
| Runtime owner (Buddy 1) | Four boot-created slots, stack capacities, pause/acknowledge/resume behavior, capacity checks, startup-complete boundary, object counters and the controller's measured stack/time headroom. |
| Driver owners (Buddies 2/3) | Permanent resource ownership, fixed transfer/RX capacities and bounded measurements of execution/transfer time and errors. Preserve the retained native UART descriptor and startup-only synchronization. |
| Configuration owner (Buddy 5) | A versioned build/resource manifest, fixed parser limits, host/target admission agreement and a bounded, read-only advice request/reply using the existing controller. Preserve fixed profile storage; network persistence needs a separate reviewed path. |
| Member 4 | Discovery ownership tests, feature/data contracts, recorder compatibility, training/evaluation, protocol-model C export, bounded inference and evidence. Proposed UI additions go to Member 5/user for review before implementation. |

Keep pool capacity and fixed RAM in the manifest/validator. Keep `active_tasks`
as the number of bound, enabled device workloads, not the four boot-created task
objects; count higher-priority workloads the same way. Idle slots still reserve
their complete stacks. The current aggregate-of-requested-stacks calculation
must be reconciled with actual fixed slot capacities. These constants need not
be learned features when they do not vary across the supported build.

### Work packages and gates

1. **Freeze the handoff and new contracts.** Reuse the recorder, provenance,
   grouped-split and baseline infrastructure. Version resource/execution-context
   changes in `timing.py`, `workload.py`, `recording.py`, `provenance.py` and the
   affected schemas/tests; preserve old readers and historical hashes. Verify
   capabilities from the integrated image rather than assuming resource capacities from an earlier
   build. An admission rejection is a capacity/validation result, not a
   negative timing-training example. A heap/object-creation violation blocks the
   build's qualification rather than becoming a behavior the model may accept.
2. **Requalify discovery and measurement on the integrated image.** Reuse startup I2C/UART
   resources without adding locks, tasks or growable buffers. Keep discovery
   restricted to an empty hardware configuration. Test BUSY/CAPACITY, repeated
   Detect -> Apply -> failed Apply -> Recovery, retained ownership after cleanup
   failures, and unchanged permanent object counts. Coordinate with the team's
   20-Apply-cycle test and parser/queue exhaustion demonstrations. Do not resume
   the user-deferred radar absence test; healthy zero/nonzero reports are equally
   valid timing observations.
3. **Characterize transferable workload costs.** Keep the eighteen-input model
   as a baseline. Before choosing additional features, measure bounded worker
   step time, completion lateness, transfer size/count, bus wait/transfer time
   and device-attributed timeouts through fixed counters/snapshots. Distinguish
   wire time from CPU execution and waiting; configured baud/clock alone does
   not describe workload. Add only useful features obtainable before the advice
   request, using independent calibration where necessary. Never use the same
   trial's age, failure or pass label as an input. Instrumentation has its own
   budget and overhead; freeze it before data collection.
4. **Collect and evaluate a new study.** Freeze the integrated firmware identity,
   resource layout, feature contract, candidate intervals, goals and whole-family
   splits before recording. Start with the existing assembly and varied valid
   I2C/SPI/UART loads; use multiple sessions without treating repeated samples as
   independent cases. Reserve new evaluation workloads before model revision;
   already revealed test cases are not untouched tests. Preserve complete bad
   runs as negatives and interrupted runs separately. Log sample freshness,
   sequence/status/source, device errors and heartbeats; a heartbeat alone is
   not a healthy reading. Separate steady-state timing evaluation from
   Apply/save/recovery acceptance and keep observer cadence fixed.
5. **Train and export only after useful held-out results.** Compare the small
   depth-limited tree with current settings, descriptor defaults and the simple
   rule. Predeclare acceptable violations, useful recommendation coverage and
   interval-choice benefit before viewing new test results. Report abstentions
   explicitly; an always-abstaining model is not successful. Keep exact device
   compatibility guards until a different supported peripheral type is tested
   unseen during training. Protocol-shaped features alone do not prove
   cross-device accuracy. The protocol model now has its own versioned C exporter;
   do not feed eighteen inputs into the old six-input template.
6. **Integrate bounded inference, then measure its effect.** Prefer the existing
   controller context with one outstanding request and a fixed candidate list
   (initially at most four). Do not consume a fifth peripheral worker or create
   a new AI task/queue/lock. Evaluate bounded work per controller step, with
   explicit BUSY/UNSUPPORTED/NO_RECOMMENDATION outcomes. Build features from
   validated typed data, not arbitrary unchecked host fields. Reuse the normal
   parser/transport under exclusive ownership; avoid duplicating whole profile
   buffers. Export immutable integer tree data; use no heap, recursion, online
   training or on-device model-file parsing. Compare Python/C predictions,
   prove resource bounds and measure loaded latency and sampling effects in the
   final image. UI advice presentation is a later reviewed integration step.

### Proposed AI allocation and acceptance

The following is a **Member 4 design proposal**, not an agreed system allocation
or measured footprint. Request at most **512 bytes of additional writable AI
state**, at most **512 bytes of incremental controller-stack use**, and a
**4 KiB code/read-only-data envelope** for the first integer-tree implementation.
Count stack use within the controller's existing reserved stack, not a second
stack allocation. Confirm it fits the team's total budget before implementation.

For scale, the current eighteen `uint32_t` features occupy 72 bytes. A depth-four
binary tree has at most 31 nodes; five 32-bit fields per node occupy 620 bytes,
plus 144 bytes for eighteen range pairs. These 764 bytes describe model tables
only, excluding code and identity/compatibility metadata. Verify actual linker
placement in flash. The proposed writable envelope also needs to contain the
bounded candidate/result/request state; new features or metadata must fit it or
trigger an explicit budget review. Do not silently grow buffers.

Retain the 10 ms observed loaded compute-latency goal as provisional, measuring
feature construction and all candidate evaluations; measure USB round-trip
latency separately. The final evidence must include RAM/flash attribution, stack
headroom, zero application allocations, zero post-startup kernel-object creation,
oversize/busy handling, C/Python parity and AI-on/off workload comparison. If the
model does not show useful benefit, retain the experimental model/results and
the normal configuration path; do not claim a failed deployment gate passed.

Contract/test design and offline exporter preparation can proceed while other
buddies implement the runtime. New deployment-qualifying captures wait for the
integrated static-memory baseline. A larger local/API language model is optional
host-side explanation work, outside this first release and outside firmware
RAM; it must never override admission or apply advice automatically.

## Protocol workload study

The current hardware covers the required minimum: BMP388 on I2C, OLED on SPI,
and LD2450 on UART. The next model studies **periodic sensor freshness under
mixed protocol load**. UART contributes a stream workload and SPI contributes a
display-refresh workload; they are not treated as interchangeable
periodic sensors. The radar baud rate stays at the verified setting.

The experimental `uthome.protocol-tree` contract and its separate C exporter
remain outside application firmware. It adds twelve configuration features:
target priority, higher-priority worker count, I2C worker count/minimum clock/
requested read rate, SPI worker count/minimum clock/requested refresh rate, and
UART worker count/minimum baud/declared bytes per second/minimum RX buffer size.
Rates in millihertz use ceiling division; absent protocols have zero counts and
rates. UART demand is frame size times report frequency (300 bytes/s for this
radar), not baud divided by ten. These are configured workload characteristics,
not measurements of CPU time, wire utilization or completed display transfers.

No device name, instance ID, outcome or desired answer enters the feature vector.
Device/build evidence still guards compatibility. Characteristics omitted from
the inputs, including display geometry, timeouts, priorities, topology and queue
depths, must match a supported context. The allowed I2C/SPI rate and clock changes
are checked against training ranges. Unknown contexts or execution shapes abstain;
they do not disable normal profile validation or require changing existing drivers.
Fallback means keep the current validated profile, with no automatic Apply and
no guarantee that an arbitrary freshness goal can be met.

The [study specification](examples/protocol-study.json) fixes twelve distinct
workload families: eight training, two validation and two untouched test groups.
It covers I2C alone, I2C with UART, I2C with SPI, and all three together, with
reviewed I2C/SPI speed and display-refresh variations. Family grouping ignores
candidate sensor intervals, goal changes, session labels and cosmetic names;
repetitions never count as independent families. These are workloads on one
assembly, not twelve independent boards or evidence for unseen peripheral types.

Every family has four intervals (including the descriptor default), two runs per
interval, 5 s warmup and 30 s observation. The fixed telemetry cadence is 137 ms.
The complete 96-run collection takes approximately one hour. The 800 ms goal is
illustrative. Freeze the study before collecting; preserve the sources matching
its hashes and create a new study if its specification or implementation changes.

The new dataset rebuilds target labels from verified raw journals and requires
fresh UART reports after warmup. A zero-target radar report is healthy transport,
not a presence failure. The OLED reads the latest-sample cache without draining
FIFO queues, so zero consumption is normal. SPI remains a configured workload;
its errors can appear in global health, but successful rendering and visible
latency are not independently measured. Complete
target/peer failures remain negative examples; missing/interrupted runs exclude
their family and block training. No automatic retry searches for passing labels.

Workflow template, only after static qualification and renewed build identity.
The old pilot identity below is historical: replace its paths and use a newly
frozen study with untouched evaluation groups before collecting:

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.protocol_study prepare --profile build/hardware-acceptance-20261026/restore-combined.json --manifest build/ai-pilot-20261026-074410/manifest.json --specification ai/examples/protocol-study.json --output build/protocol-study.json
& build/ai-venv/Scripts/python.exe -B -m ai.protocol_capture --study build/protocol-study.json --identity build/ai-pilot-20261026-074410/identity.json --restore-profile build/hardware-acceptance-20261026/restore-combined.json --port COMx --wiring-unchanged --families all --execution-context build/qualified-context.json --qualification-folder build/static-qualification --output build/protocol-capture
$runs = (Get-ChildItem build/protocol-capture/*/run-* -Directory).FullName
& build/ai-venv/Scripts/python.exe -B -m ai.protocol_study dataset --study build/protocol-study.json --output build/protocol-dataset.json @runs
& build/ai-venv/Scripts/python.exe -B -m ai.protocol_model --study build/protocol-study.json --output build/protocol-trained @runs
```

Enumerate the USB board afresh; `COMx` is a placeholder. Renew identity after
wiring/firmware changes. Collection verifies the original profile CRC, owns one
serial connection, randomizes family/interval order with a fixed seed, and
attempts normal review/Apply restoration on completion or interruption. Inspect
`summary.json` for confirmation; disconnected hardware can prevent restoration.
To stop after the current bounded run, create an empty `stop-requested` file in
the capture output directory. Individual family IDs may replace `all` to collect
separate batches; pass all their run directories when rebuilding the dataset.

Training accepts only complete, journal-verified hardware records from the frozen
study. A depth-four tree with three examples per leaf and a 900/1000 empirical
pass threshold uses training groups only; validation/test results never affect
fitting. Evaluation reports goal/health violations, abstentions and the longest
tested passing interval against current settings, defaults and the simple rule.
The leaf fraction is not calibrated confidence. This remains a small experiment;
fieldwise range checks cannot establish that every combination is reliable.

The new contract deliberately cannot enter the old six-input C exporter. Host
training/evaluation comes first; firmware integration requires a reviewed C
adapter, appropriate checks and measured benefit over the baseline. Both model
contracts retain `deployment_allowed: false`. Existing dated evidence is preserved.

### Interpretation lessons

- Zero unsafe recommendations alone is insufficient: an always-abstaining model
  may be useless. Report coverage, unnecessary abstentions and interval benefit.
- The OLED reads the latest-sample cache, not the FIFO. Zero FIFO consumption is
  expected and must not become a failed display label.
- A fresh radar report with zero targets is valid transport. Occupancy is not a
  timing label; the user-deferred absence test is separate.
- Global health totals cannot identify every failing device. Retain complete
  failures and auxiliary per-device measurements; do not discard inconvenient runs.
- A firmware/scheduler change needs new identity and measurements. Preserve exact
  profile bytes for wire CRCs; JSON reordering can invalidate preflight receipts.
- Keep all repetitions, goals and sessions of one workload in one split. Revealed
  holdouts cannot become new untouched evaluation data.

Historical study results and restoration records are in the
[completed-work log](../docs/COMPLETED_WORK_LOG.md). Their frozen gates remain
failed; moving the narrative does not change model eligibility.

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

Experiment, bundle and dataset [contracts](contracts/README.md) use version 0.1.0;
new live runs use 0.2.0. Each run identifies
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

File replay and BMP388 live serial collection are implemented. Telemetry
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

No existing model is eligible for deployment. Existing protocol studies are
USB-only experiments on the present peripherals; they do not establish wireless
load behavior, unseen-device accuracy, visible OLED latency or energy savings.
The isolated C3 parity/timing probe does not establish loaded adviser latency.
Use the [prospective study requirements](#static-core-study-and-measured-costs-27-october)
and [work packages](#work-packages-and-gates) before collecting or fitting again.

The earlier 32 KiB model-flash / 16 KiB AI-RAM planning figures are superseded by
the smaller [proposal](#proposed-ai-allocation-and-acceptance). Its limits and
10 ms loaded compute goal still require agreement against the total system
budget and measurement on the final image.

## Live recorder

`python -B -m ai.recorder --help` provides four separate commands. It reuses the
existing USB client, telemetry decoder, normal profile review/Apply workflow and
run validator. The initial supported target is an identified BMP388 on I2C0,
GPIO0/1, measuring `observed_sample_age_ms`. No firmware or UI change is needed.
Install the transport dependency with
`build/ai-venv/Scripts/python.exe -m pip install -r tools/config_transfer/requirements.txt`.

1. Preserve the original profile and close competing serial connections. Identify
   the current USB port by VID/PID/serial, then use the existing configuration
   workflow to apply an empty profile. The recorder never stops workers for you.
2. `identify` checks the retained ELF/binary against build evidence, performs the
   reviewed I2C identity probe on the idle board, and creates an observed target
   manifest. Confirm the actual wiring; do not use the host fixture as evidence.
3. Prepare the experiment using that manifest and the actual bench profile.
   `apply-trial` explicitly reviews, applies and saves one exported trial. It
   retains the exact payload and acknowledged revision/CRC in a receipt.
4. `record` only reads the already applied trial. Repeat for every planned
   interval and repetition, using one fixed telemetry cadence. Restore the
   original profile through normal review/Apply after the experiment.
5. `bundle` verifies the retained raw journals before combining records. The
   ordinary offline dataset command derives labels; incomplete scenarios remain
   excluded and complete failures remain negative.

Example commands (replace the port and paths with current bench evidence):

```powershell
& build/ai-venv/Scripts/python.exe -B -m ai.recorder identify --port COMx --build-dir build/retained-app --wiring-confirmed --output build/pilot/identity
& build/ai-venv/Scripts/python.exe -B -m ai.timing prepare --profile build/pilot/profile.json --build-manifest build/pilot/identity/manifest.json --scenario build/pilot/scenario.json --output build/pilot/experiment.json
& build/ai-venv/Scripts/python.exe -B -m ai.recorder apply-trial --port COMx --experiment build/pilot/experiment.json --identity build/pilot/identity/identity.json --interval-ms 500 --wiring-unchanged --output build/pilot/apply-500
& build/ai-venv/Scripts/python.exe -B -m ai.recorder record --port COMx --experiment build/pilot/experiment.json --identity build/pilot/identity/identity.json --receipt build/pilot/apply-500/receipt.json --interval-ms 500 --repetition 0 --cadence-ms 200 --wiring-unchanged --output build/pilot/run-500-0
# Repeat Apply and capture for every planned trial/repetition before bundling.
$runs = (Get-ChildItem build/pilot/run-* -Directory).FullName
& build/ai-venv/Scripts/python.exe -B -m ai.recorder bundle @runs --output build/pilot/runs.json
& build/ai-venv/Scripts/python.exe -B -m ai.offline dataset build/pilot/runs.json build/pilot/dataset.json
```

All outputs use fresh paths; no automatic retry, reset, flash or recovery occurs.
Apply writes flash through the existing configuration workflow, so reuse its
receipt for consecutive repetitions of an unchanged trial. `record` returns 0
for complete usable runs, including negative examples, and 2 for interrupted
runs. Setup/validation errors return 1. Inspect `assessment.json` for the label.

Each recording retains `experiment.json`, `raw.jsonl`, `run.json` and its
assessment. A preflight failure produces `failure.json` instead of claiming a
correlated run. Raw evidence is bounded to 64 MiB and 7,000 observations; cadence
is 100..1,000 ms. Actual monotonic receive times are used, including a terminal
overshoot of at most two cadences. Samples are never padded to exact endpoints.
The journal retains USB round-trip times and full telemetry for review.

Before and after recording, the active profile must return discovery BUSY with
the expected ELF hash and revision, without borrowing its I2C bus. Every sample
checks configuration revisions/CRCs, runtime generation, source mapping, uptime
and all eight counters. Fault totals include events 0,1,2,3,5,6,7; normal queue
overwrite event 4 is excluded. Runtime failures also make a complete run negative.

These are reproducibility checks, not cryptographic attestation. Physical wiring
changes and resets that leave no observable uptime/counter/configuration evidence
cannot be detected reliably without further instrumentation. Renew identity
evidence after wiring or firmware changes. Finite polling can miss intermediate
age peaks and brief faults, and the observer adds load. Keep cadence fixed and
do not describe observed sample age as worst-case latency, visible OLED latency
or energy savings. Cold-start and fault recovery need separate acceptance tests.

Further data collection still requires genuinely distinct workload families
and new untouched evaluation groups. The first twelve-family protocol study
above meets the tooling minimum but does not establish generalization. Repeated sessions,
different goals or interval candidates from one workload do not create independent
families. Compare against the simple rule before considering embedded integration.

For the larger collection, freeze the protocol before inspecting test outcomes:

- Use the existing assembly first: BMP388 alone, with OLED load, and with
  OLED/radar load once those combinations produce interpretable telemetry.
  Validate each full profile and retain its exact Apply receipt. Do not add
  actuators merely to create load or count extra repeated runs as new families.
- Audit whether the model's six inputs describe the workloads being varied.
  For example, active task count does not encode OLED refresh frequency or SPI
  transfer speed. Decide any feature changes before freezing the training and
  evaluation protocol; do not conceal omitted load factors with family names.
- Keep sibling runs, candidate intervals, goal variants and repeated captures of
  the same workload together. Preassign whole families to training, validation
  and untouched test groups. Ten families is an implementation minimum, not a
  guarantee of adequate evidence or generalization to another board.
- Retain complete goal failures and stale/fault cases. Investigate recorder
  interruptions separately; do not retry until a passing label is obtained.
- Train only after the real dataset passes validation, report goal violations
  and abstentions alongside interval choice, and compare with defaults and the
  existing simple rule. Deploy only after useful held-out benefit and measured
  target resource costs have been demonstrated.

## Existing local model

On this workstation, the model is
`build/ai-offline-20261013/trained/model.json`; evaluation, C export and compile
checks are beside it. Runs and the dataset are in the parent output directory.
The model was reformatted for readability; its canonical JSON still matches the
recorded model hash. Preserve the dated evidence rather than replacing its
original byte counts/hashes after formatting.

These generated files and `build/ai-venv/` are ignored, not committed binaries.
A new chat on this workstation can reuse them; a fresh clone must regenerate
them using the commands above. Verify source/build identities before reuse; these are historical artifacts,
not a qualified model for the current firmware.

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

The eighteen-input `protree` template and `pro_main` harness received the same
rules 1-8 review: typed immutable arrays, checked NULL/exact count/domain,
validated acyclic child indices, at most five visits, no casts/heap/recursion/
mutable globals, documented caller compatibility checks and preserved disabled
deployment. The exporter rejects mismatched model metadata; tests cover the
eighteenth input, threshold equality, domain boundaries and the maximum 31-node
tree. `workcost`'s separate ownership/arithmetic review is in the
[runtime guide](../firmware/src/runtime/README.md#workload-measurement-handoff).

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

Generated model C is checked explicitly even though `build/` is excluded from
the repository source walk. Passing checks do not certify all standard rules.

## Static-core study and measured costs (27 October)

This heading is retained for existing references. Completed studies and target
probe details are in the [history log](../docs/COMPLETED_WORK_LOG.md), with original
records under `qualification/`. Use these lessons for the next prospective study:

- SPI polling overhead can dominate nominal wire time. The corrected adapter
  permits a 1 ms polling burst before yielding; 400 kHz still misses the tested
  20 ms frame deadline. Keep earlier negative data tied to its original firmware.
- UART baud is capacity, not continuous traffic. The nominal descriptor input is
  300 bytes/s; this module/settings measured about 19.1 reports/s (574 bytes/s).
  Independently calibrate actual stream rates; do not rewrite frozen inputs.
- Fixed goals can remove any optimization opportunity: the primary training
  families offered only a best 500 ms choice or no passing choice, with six
  constant inputs. A tree cannot demonstrate longer-interval benefit absent from
  its labels. See the [training-only review](qualification/primary-training-design-review-20261027.json).
- Use supported exact clock divisors and admission preflight before freezing a
  study. An activation rejection is not a negative timing label.

`ai.protocol_capture --measure-costs` requires command-9 observations every
second, exact firmware/profile/generation identity, monotonic counters, valid
source bindings and controller/idle stack headroom. Startup/normal allocation
counters must stay zero. Missing, saturated, changed or reset measurements
interrupt the run without automatic retry. `load_recording` verifies retained
work-cost data against raw journals and hashes. Complete goal failures remain
negative data. Worker cost is wall time, not isolated CPU or electrical wire time.

`ai.protocol_gate` rebuilds verified data and reports family-level violations,
unnecessary abstentions and benefit against the simple rule. Its existing gain
definition requires longer passing intervals or safe coverage where the rule
has none, with no regression against passing rule choices. Both completed
models failed that frozen definition. Synthetic adversarial labels are tests,
never measurements, and the gate never enables deployment by itself.

For the next study, first qualify the static/radio context. Independently
calibrate protocol costs and actual rates, predeclare varied goals/candidates
and keep related goal/session variants together. Compare with current/default
settings, the simple rule and an analytical protocol-cost baseline. A display's
payload and SPI clock already bound its minimum transfer time; learning that
bound alone does not establish ML benefit. Test a held-out supported peripheral
type before claiming cross-device accuracy. Unknown contexts must abstain while
normal configuration remains available.
