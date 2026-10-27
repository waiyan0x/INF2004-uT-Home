# AI timing pipeline

The original six-input model is an **experimental tree trained on simulated data**.
The eighteen-input protocol model has historical and current real-data records
below. The latest two USB static-core studies total **272 usable runs and 99,552
observations**. Both model contracts
predict whether a candidate interval meets the recorded goal
and health criteria; neither is enabled in ordinary firmware or the UI Apply flow.
The primary model has a separately selected target parity/timing diagnostic.
Device identity, protocol and wiring stay in the reviewed
[discovery/catalogue flow](../docs/README.md).

The [mandatory static-memory change](../firmware/README.md#mandatory-static-memory-change)
now takes priority before deployment qualification. Preserve both measured
studies as evidence for their original builds. Worker/resource lifecycle changes
require fresh timing evidence on the compliant firmware; the existing host
pipeline remains reusable. Budget embedded inference as immutable model data
plus fixed feature/work buffers and bounded stack use. No model is deployed.

## Static-memory implementation checkpoint

The user has since authorized taking over all remaining system work. Follow the
[overall delivery plan](../README.md#overall-delivery-plan) across runtime,
configuration, drivers, measurements, AI and Desk integration. The prerequisite
work below remains incomplete, but is now included in this implementation effort
rather than waiting on another member. The historical roles and measured results
remain unchanged. This guide owns the detailed AI contracts and evaluation gates.

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

The authorized Member 4 work now has a versioned execution sidecar, a gated
collector, fixed measurement helpers and a separate eighteen-input C exporter.
See [verification](qualification/static-adaptation-20261026.json). The integration
application was installed after a verified full flash backup. Twenty bench
cycles passed active-profile BUSY checks, idle I2C/UART discovery, target rejection
of an invalid task-capacity setting, and restoration with fresh BMP388/radar
samples. The user confirmed the OLED readable and updating. This is lifecycle
regression evidence, **not** zero-allocation or fault-recovery qualification.

| Work package | Current result |
|---|---|
| Shared contracts | `static_context.py` and `execution-context.schema.json` distinguish active workloads from all four reserved stacks, constrain candidate/state budgets, and bind firmware/profile/evidence hashes. |
| Measurement/export preparation | `workcost.c/.h` provides caller-owned saturating counters; `measurements.py` checks snapshots and deltas. `protocol_export.py` generates bounded C for the eighteen-input model. |
| Fixed runtime core | Typed parser pools, four permanent 8 KiB worker stacks, a fixed 1 KiB idle stack and checked storage are implemented. The allocation-audit image passed two 909-check fixture boots, twenty cycles, rollback and reboot restore; application startup/normal allocation and post-startup object counters remained zero. Stack measurements and whole-system qualification remain open. |
| Discovery acceptance | Twenty cycles on the instrumented core passed busy ownership, idle I2C/UART discovery, capacity rejection and fresh sensor restoration without new allocations/objects. Isolated UART faults and wider electrical acceptance remain deferred. |
| On-device wireless baseline | Required Wi-Fi/BLE and network services are not integrated. Compatibility, fixed infrastructure resources and traffic/load coverage must be resolved before the next qualified study. |
| Fresh scoped studies | Primary: 192 runs / 70,272 observations; corrected stress: 80 runs / 29,280 observations, both with profile restoration. The seven-node and five-node models passed Python/C parity but both failed the unchanged benefit criterion. See [primary](qualification/protocol-static-study-20261027.json) and [stress](qualification/protocol-stress-study-20261027.json). Radios and unseen peripheral types remain outside their scope. |
| Model release | No model is eligible for deployment. The primary classifier passed two isolated target parity/timing boots; this does not replace its failed statistical gate or qualify loaded inference. Ordinary firmware and the Desk remain unchanged by these experimental models. |

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
profile revision. A future controller wrapper must implement those checks and
normal reviewed Apply. This seven-node object measures 400 bytes code/read-only
data, zero writable globals and compiler-reported zero stack bytes for the
classifier alone; these are standalone object results, not whole-request or
linked-firmware measurements. The largest accepted 31-node tree has 764 bytes
of tables. An unseen supported peripheral is still required before claiming
cross-device accuracy.

## AI plan for the agreed static-memory design

### Implemented bounded controller preparation

The [preparation record](qualification/controller-preparation-20261027.json)
binds source snapshots, the 111-test AI regression, five additional feature tests
and standalone C3 object measurements. It does not permit deployment.

`firmware/src/services/adviser.c/.h` now provides a portable, caller-owned
432-byte state machine with at most four copied eighteen-input candidates.
It evaluates one candidate per step and returns the longest passing interval.
Every step and reply checks the profile CRC/revision, session, build and execution
context; stale identity, reversed clocks, a 100 ms scheduling deadline and a
30-second result lifetime invalidate advice. Unsupported/unqualified contexts
never invoke the predictor. The separate 10 ms inference-time goal remains to
be measured on the target.

Nine compiled C99 host tests cover cancellation, stale results, bounded capacity,
invalid predictors, timeout and preservation of an existing busy request. The
C3 object evidence under `build/system-completion-20261026-233938/adviser-object/`
records 914 bytes of code/read-only data, zero writable globals, and a largest
compiler-reported function stack of 32 bytes. This is not a whole-call-chain or
linked-target timing qualification.

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

`adfeat` has no writable global state; its standalone C3 object uses 706 bytes
of code/read-only tables and a compiler-reported 112-byte function stack.
Manual review: at most eight devices are visited, divisors are nonzero and bounded,
and rate sums cannot exceed eight million before the versioned bounds check.
Only complete vectors are published. The caller must retain a stable admitted
plan and provide a nonaliasing output; the function is not another admission or
model-compatibility authority. A versioned advice endpoint, release-model binding
and reviewed Desk presentation remain outstanding.

### Agreed design context

The following Desk proposal is prepared for review after the user wakes; no UI
code or visual styling has changed. Add one collapsed **Timing advice** card in
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

A separate prospective stress extension was initially frozen locally under
`build/protocol-study-20261027-stress-r1/`, with ten new workload families and
80 planned recordings. That plan was not executed. Its corrected r3 successor
has now completed; see the result near the end of this guide. The extension
varies SPI loads below the primary study's 1 MHz lower boundary.
`selected-plan.json` retains the pre-capture feature-domain amendment: one
development clock is 800 kHz so the unchanged 500 kHz test is an interpolation
case; the original unexecuted plan is preserved. This selection preceded the
primary study's label/evaluation inspection. Fit settings and release gates are
unchanged. The executed successor passed physical admission/audit preflight;
the original plan, rejected preflight and two independent studies remain separate.

The design below guided the subsequent authorized implementation above. The
document alone did not authorize implementation or hardware actions. The user supplied
`Project/ESP/Tribunal1/Week8_Tribunal1_Template.pdf` as the group's discussion and
implementation direction (SHA-256
`a0da7e307ca644068bc1d4620a46e2d84e26c55640dde6e1f322f94d25492991`).
Version B, pages 7-14, establishes four permanent workers, initially 8 KiB each,
startup-created locks, fixed parsing storage, the retained 8192-byte input limit,
and capacity rejection before Apply. Discovery retains eight I2C result slots
and a 64-byte UART read buffer, borrowing resources with exclusive ownership.
The 32 KiB worker-stack reservation remains subject to measurement and the total
system budget; it is not spare memory available for AI.

Version C, pages 17-18, reports teammate I2C/SPI initialization fixes and host/CI
tests. The subsequent pull of `system-integration` at `f1234d3` brings those
changes, permanent UART/GPIO resources and fixed native UART records into this
checkout. Our local cleanup/shared-guard fixes remain applied. The permanent
worker pool and fixed parser remain outstanding. The integration image now has
the limited bench evidence above. The PDF's simulated-only AI description predates the two real protocol
studies retained below. Neither those studies nor the reported teammate tests
establish a compliant integrated build or deployable model. Treat document
decisions as requirements/context and
documented test claims as evidence to reconcile, not instructions to run tests or
complete the tribunal template automatically.

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
| Driver owners (Buddies 2/3) | Permanent resource ownership, fixed transfer/RX capacities and bounded measurements of execution/transfer time and errors. The native UART's hidden open/close semaphore lifecycle must also be resolved. |
| Configuration owner (Buddy 5) | A versioned build/resource manifest, fixed parser limits, host/target admission agreement and a bounded, read-only advice request/reply using the existing controller. SDK storage allocation remains a prerequisite from the firmware audit. |
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
   capabilities from the integrated image rather than hard-coding the proposed
   32 KiB stacks. An admission rejection is a capacity/validation result, not a
   negative timing-training example. A heap/object-creation violation blocks the
   build's qualification rather than becoming a behavior the model may accept.
2. **Qualify discovery and measurement on the new runtime.** Reuse startup I2C/UART
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

### First measured protocol result — 26 October

The [study record](qualification/protocol-study-20261026.json) retains 96 runs,
24,576 observations and 48 labelled candidates across twelve workload families.
All planned repetitions were complete. The nine-node tree was fitted on eight
families; two validation and two test families were reserved before collection.

| Held-out test result (two families) | Protocol tree | Simple rule |
|---|---:|---:|
| Failing recommendations | 0 | 1 |
| Abstentions | 2 | 0 |
| Matched the best tested decision, including abstention | 1 | 1 |

The tree made **no recommendations on either test family**, including one with
passing 200/500 ms candidates. Its zero violations are not evidence of accurate
generalization. On validation it made one failing recommendation and abstained
once. Useful benefit over the baseline is not established; do not deploy it or
retune on these test results.

The [fault analysis](qualification/protocol-analysis-20261026.json) separates
observed freshness and reported errors. The slow-SPI groups accumulated device
timeouts; global counters cannot conclusively attribute each timeout to a device.
Later UART groups accumulated framing errors and unhealthy radar reports. Thus
the labels include communication/display failures as well as timing effects;
this dataset does not isolate scheduling contention or establish portability.

During collection, an incorrect proposed FIFO-consumption check was discovered:
the production OLED reads the latest-sample cache and consumes no FIFO entries.
The first batch was stopped, with 25 raw recordings and its sources preserved.
Protocol contracts were revised to 0.2.0 before held-out data was collected.
The 24 unchanged recordings from three completed training families were replayed
under the corrected policy; the single run from the interrupted fourth family
was kept separate. The other 72 planned runs were collected under the new freeze.
`build/protocol-study-20261026-r2/reuse.json` records that selection. No test outcomes
were used to fit or change the model.

The original combined profile was restored active/saved at revision **87**, CRC
**3623560383**. A separate post-restoration query found fresh BMP388 data but a
radar fault with new timeouts; the earlier UART reliability issue remains open.
No firmware image or wiring was changed by this study. All 76 AI tests passed,
including 11 new protocol tests and the 194-file full BARR-C gate.

The model, evaluation and dataset are in
`build/protocol-study-20261026-r2/trained/`. Keep both capture batches and frozen
source copies. Next: resolve or independently characterize communication faults,
add measured execution/transfer information where justified, and predefine new
untouched evaluation groups before revising the model. A held-out peripheral
type is still needed for a cross-device accuracy claim. Unknown-context fallback
already preserves normal configuration; it is not an accuracy claim.

The subsequent [shared-guard correction](../firmware/qualification/esp32c3-shared-handoff-20261026.json)
changes the kernel runtime's scheduling behavior. The earlier model and study
remain evidence for their original build; do not rewrite their identities or
source hashes, or reuse their labels as measurements of the updated firmware.
Before another study, capture a new board/build identity, freeze a new plan and
evaluation groups, and collect fresh repetitions. The user deferred radar
zero-target acceptance. Target count is not a timing label: check report freshness
and communication health independently of presence. The combined-load bench
capture is diagnostic evidence, not a replacement for journalled AI trial runs.
No model was retrained or deployed during this runtime correction.

### Post-fix measured result — 26 October

The subsequent [study](qualification/protocol-study-shared-20261026.json) collected
**96 entirely fresh runs**, 24,576 observations and 48 labelled candidates on the
corrected firmware. It used the eight original development workloads, with two
new validation and two new test workloads absent from the entire first study.
The [frozen specification](examples/protocol-study-postfix.json) retains their
selection. Repeated observations are not independent training examples.

The fixed trainer produced a **seven-node tree**. No thresholds or hyperparameters
were changed after viewing the new outcomes.

| Evaluation | Tree wrong recommendations | Tree abstentions | Simple-rule wrong recommendations |
|---|---:|---:|---:|
| Validation: two workloads | 0 | 1, appropriate | 1 |
| Test: two workloads | 0 | 1, unnecessary | 0 |

The tree correctly recommended 500 ms in two reserved workloads and declined
the failing 1 MHz SPI workload. It also declined the 2 MHz SPI workload even
though 200/500 ms passed both repetitions. Its learned 2.5 MHz boundary rejected
that case despite matching context and in-range inputs. This is a generalisation
gap, not an unsupported-peripheral fallback. The predeclared requirement for no
unnecessary held-out abstentions **failed**; target benchmarking and deployment
remain disabled. The model must not be presented as better than the simple rule
on the test partition.

The [analysis](qualification/protocol-analysis-shared-20261026.json) retains the
decision path and health totals. All observed target and UART samples after
warmup were fresh. No new missed heartbeats, UART framing/receive-overrun or
device-I/O events appeared. There were 1,923 global device-timeout events, confined
to the three 1 MHz SPI workload groups. Their device attribution and transfer
durations were not measured; they remain negative examples, not discarded runs.

The original profile was restored active/saved at revision **138**, CRC
**3623560383**, and a later query found fresh BMP388 and radar readings. No firmware
image, wiring or UI was changed by the study. All 76 AI tests and the full
194-file C gate passed before collection. An initial preflight aborted with zero
trials because a reordered JSON copy changed the wire checksum; using the original
profile bytes resolved it without changing settings, groups or measured runs.

Artifacts are in `build/protocol-study-20261026-r3/`; the completed recordings are
in `capture-verified/`, and model/evaluation/decisions/readiness are in `trained/`.
Preserve the frozen source copies. Next: characterize transferable execution and
transfer costs or expand development coverage, then reserve new evaluation groups
before changing the model. Do not edit its threshold to fit the revealed test.
No unseen-device accuracy, visible OLED latency or energy benefit is established.

Splits precede fitting and all related repetitions stay together, following the
[scikit-learn guidance on data leakage](https://scikit-learn.org/1.8/common_pitfalls.html#data-leakage).

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

The [2026-10-13 record](qualification/offline-20261013.json) contains 48 scenarios,
20 families, 576 runs and 192 candidate examples. The 21-node tree chose the best
tested interval in 8/10 held-out scenarios; the simple rule achieved 9/10. Both
missed the goal once. There is no demonstrated reason to replace the rule.

C matched Python on 1,248 vectors. A standalone C3 compile reported 584 bytes of
code/read-only data and no writable static data; it was not a linked firmware
build or a board measurement. All recommendations remain non-deployable.

The [26 October recorder pilot](qualification/recorder-pilot-20261026.json)
captured eight real BMP388 runs: two repetitions of each interval, with 5 s
warmup and 30 s observation per run. At 137 ms telemetry cadence, all 2,049
observations were fresh and no new fault events occurred. The 789 normal queue
overwrites were retained separately. The illustrative freshness goal was 800 ms.

| BMP388 interval | Maximum observed age across both runs | Both repetitions meet goal |
|---|---|---|
| 200 ms | 198 ms | Yes |
| 500 ms | 499 ms | Yes |
| 1,000 ms | 997 ms | No |
| 2,000 ms | 1,988 ms | No |

Journal verification produced four candidate rows with no exclusions. The largest
tested passing interval was 500 ms, also selected by the simple rule. This is
one board and one workload family, so no real-data model was trained. OLED and
radar stayed wired but disabled during the trials; the original combined profile
was restored active and saved at revision 36, CRC 3623560383. BMP388/radar were
fresh in the restoration snapshot; this does not close their reliability tests.

The 65-test AI suite passed, including 15 recorder tests and existing generated-C
checks. Its full BARR-C gate reported 194 files, zero findings and zero tool/policy
errors. No firmware C changed for the recorder. Raw journals, identity, Apply
receipts, dataset and restoration evidence are retained locally in
`build/ai-pilot-20261026-074410/`; the qualification record indexes their hashes.

Next: broader measured training data, untouched real evaluation, then
integration/resource measurements if beneficial. Physical acceptance remains
a separate requirement; an AI timing capture does not complete it.
The earlier provisional 32 KiB model-flash / 16 KiB module-RAM targets are
superseded for planning by the smaller
[static-memory proposal](#proposed-ai-allocation-and-acceptance). All proposed
budgets and the retained 10 ms loaded compute-latency goal still need measurement
and integration into the team's total resource allocation.

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

The eighteen-input `protree` template and `pro_main` harness received the same
rules 1-8 review: typed immutable arrays, checked NULL/exact count/domain,
validated acyclic child indices, at most five visits, no casts/heap/recursion/
mutable globals, documented caller compatibility checks and preserved disabled
deployment. The exporter rejects mismatched model metadata; tests cover the
eighteenth input, threshold equality, domain boundaries and the maximum 31-node
tree. `workcost`'s separate ownership/arithmetic review is in the
[runtime guide](../firmware/src/runtime/README.md#workload-measurement-handoff).
The current full gate covers 229 owned C/H files with no findings or tool/policy
errors. Historical counts below remain tied to their original artifacts.

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

## Static-core study and measured costs (27 October)

The [USB-core checkpoint](../firmware/qualification/esp32c3-workcost-20261027.json)
provides the execution context for a new 192-run study under
`build/protocol-study-20261027-static-r2/`. Its frozen source tree, specification,
identity and predeclared evaluation are retained there. Sixteen families are
development data; four validation and four test families were reserved before
capture. Earlier revealed holdouts may be used as development coverage, never
as fresh independent evidence. The new evaluation combinations are distinct.

The [completed result](qualification/protocol-static-study-20261027.json)
retains 192 usable runs and 70,272 observations with no capture/restore error.
The seven-node tree makes five safe 500 ms recommendations and correctly
abstains on three of the eight held-out families; the simple rule recommends
500 ms for all eight and violates the peer-health criteria on those same three.
There are no unnecessary abstentions or regressions against safe rule choices.
This is evidence of useful safety abstention, but **not a pass** of the frozen
benefit definition below: there was no safe coverage or longer-interval gain.
Do not relabel the criterion after seeing these results.

The exported C matches Python on 1,165 vectors. Its standalone C3 object uses
400 bytes of code/read-only data and no writable globals; this is not linked
target latency or combined-system evidence. The subsequent
[two-boot target probe](qualification/model-target-probe-20261027.json) matches
all 1,165 vectors on both boots, with maximum single prediction 54 microseconds
and four-candidate batch 83 microseconds (clock/call overhead and preemption
included). Successful SDK allocation and native allocator/object counters did
not change during inference. That linked-build classifier object uses 442 bytes,
zero data/BSS and a zero-byte compiler leaf frame; the 88,612-byte test corpus is
separate. These compiler frames are not a complete measured stack high-water mark.
No loaded sampling/AI comparison or complete adviser request was measured.
Ordinary application/radio builds reject linked model symbols. The model is not
deployed as advice.

Auxiliary measurements identify 9,652 SPI transfer timeouts in the lower-clock
workloads, with zero I2C or UART transfer errors. The adapter's sleep after
25 ten-microsecond polls delayed each 64-byte FIFO chunk. The
[corrected polling bench](../firmware/qualification/esp32c3-spi-polling-20261027.json)
now passes 500 kHz through 4 MHz with no errors; 400 kHz still exceeds the 20 ms
deadline. Preserve the negative recordings and their original firmware identity;
the learned boundary includes this earlier driver behavior. UART API payload rates measured
about 573–575 bytes/second, whereas the existing input is explicitly a nominal
300-byte/second descriptor value. An independent sequence-delta reconciliation
across 120 windows measures about 19.12–19.15 reports/second, explaining roughly
574 bytes/second for this module/settings. The readonly result is retained as
`static-r2/analysis/uart-rate-reconciliation.json`. Nominal descriptor inputs were
not rewritten after training. Neither measurement establishes electrical wire
timing or CPU utilization; other radar settings require fresh measurement.

`ai.protocol_capture --measure-costs` requires command-9 observations every
second, exact firmware/profile/generation identity, monotonic counters, valid
source bindings and measured controller/idle stack headroom. Startup and normal
allocation counters must remain zero. Missing, saturated, changed or reset
measurements interrupt the run without an automatic retry. `load_recording`
replays these observations and verifies the retained `workcost.json` against
the raw journal and run hashes. Completed goal failures remain negative data.
The full AI suite passes 97 tests with the new capture/readiness checks.

Work costs are auxiliary characterization, not additional model inputs or CPU
utilization. The eighteen configuration features and fixed tree settings remain
unchanged for this study. Results cannot establish unseen-device accuracy or
wireless coexistence, and do not authorize deployment on another firmware hash.
The existing model remains experimental; the original profile is restored by the
collector at completion or failure.

`ai.protocol_gate` rebuilds the verified dataset and reports family-level
violations, unnecessary abstentions and benefit against the simple rule. A gain
means a longer passing interval or safe coverage where the rule has no passing
choice, with no regression against any passing rule choice. This operational
definition was implemented before inspecting the new holdout labels. It never
turns on deployment: target parity, resource/latency, combined-system and reviewed
Desk integration remain separate gates. Its adversarial unit labels are synthetic,
kept in memory, and never mixed with measurements.

The first static-core attempt remains preserved under `static-r1`: it restored
after a 3.5 MHz SPI activation rejection. Before inspecting any heldout labels,
the revised study replaced those two clocks with 3,333,333 Hz, preflighted all
24 families, and began a wholly fresh dataset with unchanged model settings.

The [completed corrected stress study](qualification/protocol-stress-study-20261027.json)
retains 80 usable runs and 29,280 observations, with successful restoration.
Its five-node model matches Python/C on 1,108 vectors. All four held-out families
receive safe 500 ms choices from both the model and simple rule: no violations,
unnecessary abstentions or interval/coverage gain. The unchanged benefit gate
therefore fails, and this model is not deployed. I2C and UART windows have no
transfer errors; negative SPI deadline results remain in the dataset.

The study is independently frozen under
`build/protocol-study-20261027-stress-r3/`: ten families (six train, two validation,
two test), two repetitions of four intervals, 45-second windows after five seconds
of warmup, and the same 18 inputs/estimator/gates. It evaluates new combinations
on the present supported devices; it cannot support unseen-peripheral claims.
The r2 preflight rejected the first 100 kHz activation and restored the baseline
before collecting labels. Its preserved amendment selects exact SPI divisors at
400 kHz or above; I2C/refresh/UART/split/model settings are unchanged. Check
`capture/summary.json` and `analysis/trained/gate.json` before stating an outcome.

The [training-only design review](qualification/primary-training-design-review-20261027.json)
explains the primary study's limited optimization opportunity: all sixteen
training families have the same 800 ms goal; twelve have a best passing candidate
of 500 ms and four have no passing candidate. The simple rule always chooses
500 ms. The model can learn when to abstain, but these labels supply no alternative
safe interval to beat that rule. Six inputs are constant in this training set.
The review does not retune the model, reinterpret held-out outcomes, alter the
frozen gate or change the separately frozen stress study.

The next protocol-model study should first use a completed static/radio context,
then independently calibrate protocol service costs and actual stream rates.
Predeclare varied operational goals and meaningful candidate ranges, retain
simple-rule/current/default baselines, and group all goal/session variants of
one workload together. Reserve another supported peripheral type before making
cross-device claims. Unsupported or uncalibrated contexts must keep normal
validated configuration available while advice abstains. A larger host language
model cannot substitute for these measured embedded-model checks.

Include an analytical protocol-cost baseline in that future comparison. For
example, a known display payload and SPI clock already give a lower bound on
transfer time; predicting a deadline miss explained entirely by that bound is
not evidence that ML adds value. Model benefit should address measured scheduling,
contention or changing service load beyond those deterministic checks. This is
a prospective design requirement, not a retroactive change to either completed
study's frozen evaluation.
