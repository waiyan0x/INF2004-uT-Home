# Platform integration checkpoints

Current direction (2026-10-12): the software baseline is ready for offline
preparation, while physical acceptance remains partial. The user has prioritised
device discovery and recommended protocol/settings before model training. Start
with the new D1–D4 sequence at the end of this plan. Earlier entries retain their
historical checkpoint results and deferrals.

Updated 2026-09-28. Complete the reusable platform before AI. Keep each change
small, preserve existing interfaces where practical, and review each checkpoint
before starting the next. The user now requests peripheral hardware testing at
final acceptance; software checkpoints may proceed meanwhile.
The user explicitly authorized completing checkpoints 6c and 7 together, then
reviewing; hardware tests with the team and AI remain deferred.
The user authorized taking on the remaining member
integration work; this does not change the original authorship of contributions.

Current scope: checkpoints 6c and 7 are the software completion boundary.
The final section records validation; checkpoints 8 (hardware) and 9 (AI) wait
for the team session and subsequent AI work.

## Checkpoint 1: baseline and contracts (completed)

- Preserved 196 working-tree files, two tracked deletions, staged/unstaged
  patches, the original Git index and a verified Git history bundle. The pinned
  kernel worktree was clean. Ignored build outputs are outside this snapshot.
- The local backup is `%TEMP%/uthome-integration-baseline-5yetkvgg/`; its
  `snapshot.json` records paths and SHA-256 hashes. `working-tree.zip` preserves
  the files; `index.bin` and the patches preserve staging. Recover into a
  separate directory and compare before replacing any current work.
- GitHub refresh was attempted with prompting disabled and failed because
  credentials were unavailable. The commits below are locally cached sources,
  not confirmation of the latest upstream state.
- Added a small shared sample header and queue-exchange tests. Ownership,
  cleanup, deployment and memory decisions are in
  [firmware/CONTRACTS.md](firmware/CONTRACTS.md).
- Host admission now rejects competing enabled consumers of one FIFO and
  duplicate source references. Multiple producers remain supported and samples
  identify their source instance. Target routing enforcement is still pending.
- Peripheral and UI imports are inventoried below. They have not been merged
  or copied into this checkpoint; import them as their modules are integrated.
- Existing target worker source and qualification records remain unchanged.
  New sample records do not claim new board qualification.

The original baseline has 63 audited C/header files, 109 automated test passes,
and two board runs of 369 checks each. See [the historical audit](tools/c_standard/AUDIT.md).
Record new checkpoint checks separately; do not rewrite those results.

Checkpoint validation on 2026-09-28:

| Check | Result |
|---|---|
| Mandatory BARR-C gate, pinned clang-format 20.1.8 | 66 C/header files; zero findings or tool/policy errors |
| Checker regression suite | 54 passed |
| Foundation suite, including route admission | 26 passed |
| Runtime C suites, including copied samples | 18 passed |
| Target failure/build-tool suite | 15 passed |
| Sample layout compile for ESP32-C3 | C99, warnings as errors; all four records fit 64-byte items |
| New board run | Not performed; target worker implementation is unchanged |

The 113 automated test passes include the existing intentional overlap of C
fixtures between Python suites. Raw local logs and the compile-only target
object are under `%TEMP%/uthome-contract-checks-u5nt8uf2/`.

Manual C review: new records use fixed-width integer fields, finite-value float
contracts, explicit Boolean presence and no pointers or allocated storage in
headers. They add no kernel calls, casts, concurrency primitives or deviations.
The C99 tests check copied ownership, per-source identity, validity status and
target layout bounds. Existing queue/worker synchronization remains a later
checkpoint; this header does not make those modules thread-safe.

## Checkpoint 2: target activation (completed)

Added two small modules: `activate` owns whole-plan replacement/recovery and
`shared` serializes queue/health/watchdog operations. The existing worker port
adds a start barrier and a nonblocking shared-operation guard. No driver names,
JSON parser, HAL imports or AI logic were added. See
[the API and manual review](firmware/src/runtime/ACTIVATION.md).

Plans validate names, contexts, task/queue bounds and declared consumer routes
before stopping anything. Failed activation reconstructs preceding settings;
failed stop/cleanup/restoration retains ownership for explicit recovery. The
initial target path supports periodic workers. Descriptor/wiring resolution,
stream readiness and application orchestration remain separate checkpoints.

| Check | Result |
|---|---|
| Mandatory BARR-C gate, pinned formatter | 70 C/header files; zero findings or tool/policy errors |
| Host regression suites | 54 checker + 26 foundation + 18 runtime + 15 target = 113 passed |
| New activation/shared modules | Separate ESP32-C3 C99 compilation, warnings as errors |
| Real target build | Passed; 169,472-byte application; exact sources hashed |
| COM8 acceptance | Two USB-reset runs, 514 checks each, including queue exchange, replacement and recovery |

Fresh [build/board evidence](firmware/qualification/esp32c3-activation-20260928.json)
preserves the historical records. The application-only flash kept the existing
bootloader and partition table; a verified 4 MiB backup of the preceding image
is retained. The board now runs the activation acceptance image. These fixture
results do not qualify external peripherals or durable configuration recovery.

Changes were tested in a checked copy under
`%TEMP%/uthome-activation-u7552i33/`; `before.zip`, `before.json` and
`index.bin` preserve the preceding working tree and index. The C11 atomic
profile remains confined to its approved files; no exception was added.

## Checkpoint 3: I2C/BMP388 software (hardware acceptance pending)

The portable I2C contract, C3 FIFO backend, deterministic host register fake,
BMP388/Bosch wrapper and component/worker adapter are implemented. The bus owns
one controller and serializes transfers; drivers borrow it and claim addresses.
Two configured instances share a bus without adding device branches to the
runtime. See [integration and manual review](firmware/drivers/bmp388/INTEGRATION.md).

One deadline covers each driver operation, including mutex contention and
required delays. Failed measurements produce zeroed typed error records.
Cleanup retains ownership until a touched sensor can be put to sleep. Sensor
initialization, duplicate claims, faults and restoration are tested through
the existing registry and activation manager.

Validation: 82 owned C/header files pass the mandatory gate; 54 checker,
26 foundation, 18 runtime and 16 target tests pass (114 total). The target
qualification now also verifies effective compiler commands: ordinary owned
modules use C99 (GNU99 for the C3 adapter's external SDK headers), the two
approved atomic files use C11, and SDK warning-error relaxations are overridden.
The [new evidence](firmware/qualification/esp32c3-i2c-20260928.json)
records the exact build and remaining hardware limits.

No board flashing or peripheral I/O was performed in this checkpoint. The user
has a BMP388 available but not wired. Confirm the breakout, supply/pull-ups,
I2C address and SDA/SCL wiring before completing physical acceptance. The
target-pending peripheral manifest remains empty; checkpoint 3 is not yet
hardware-complete. SPI/OLED and the other checkpoint 4 modules remain untouched.

The checked working copy and preceding 210-file snapshot are retained under
`%TEMP%/uthome-i2c-qxvy2g1q/`. Vendor bytes, the kernel pin, previous board
records, the Git index and unrelated working changes are preserved.

## Checkpoint 4a: SPI/OLED software (hardware deferred)

Added reusable SPI, the C3 SPI2 FIFO backend, shared pin ownership, checked digital
outputs and caller-owned SSD1306 frames. I2C now reserves pins through the same
ledger. The OLED adapter uses the existing registry and periodic worker; runtime
and sample formats are unchanged. See
[binding, limits and manual review](firmware/drivers/ssd1306_spi/INTEGRATION.md).

One deadline spans mutex waiting and FIFO chunks within one CS window. Failed
refresh retains dirty pixels; failed cleanup retains ownership for recovery.
Serial OLED has no acknowledgement, so SPI cannot prove panel presence.

Validation: 99 owned C/header files pass the gate and all 114 host tests pass,
including extended SPI/OLED, pin and activation scenarios. The real C3 build
passes with 49 inputs and a 188,208-byte app. Effective compiler profiles and
three excluded SDK-header GNU99 boundaries are recorded in
[fresh evidence](firmware/qualification/esp32c3-spi-20260928.json).
Policy, approved deviations, vendor bytes, the index and earlier board records
are preserved. The preceding 229-file snapshot and checked copy remain under
`%TEMP%/uthome-spi-u9pdvfu7/`.

No flash/reset/peripheral operation was performed. Hardware checks wait until
checkpoint 8. Queue-to-pixel formatting remains display-service work; conditional
GPIO2 CS remains unsupported by this backend. Target capabilities stay empty,
AI stays last, and UART/LD2450 software is next.

## Reuse and adaptation inventory

| Owner / cached source | Reuse | Required adaptation before integration |
|---|---|---|
| Member 1: `feat/runtime-utkernel-adapter` at `2cd8741` | Runtime, queues, health and fault-test designs reconciled locally; periodic target activation/recovery now qualified with fixtures | Add stream readiness and integrate real driver resource ownership. Host snapshots cannot restore live hardware. |
| Member 2: `feat/ssd1306-driver` at `56f718e` | I2C/BMP388 design adapted with independent contexts, deadlines, real delay, locking and typed samples; Bosch sources preserved | SPI/OLED is now adapted with caller-owned frames and C3 transport; queue formatting and final physical tests remain. |
| Member 3: `feat/uart-gpio` at `974eb9c` | UART parser, GPIO/pulse/PWM contracts, host simulation and tests | Implement C3 backends; preserve UART single ownership; add checked cleanup; convert microsecond timestamps to the common millisecond contract; map errors explicitly. |
| Member 5: `system-integration` at `bab0924` | Local review UI, workflow, packaging and integration-test scaffolding | Use node schema 2.0.0 and the shared planner; remove fixed driver lists and older resource limits; add actual USB deployment and real telemetry. Filesystem bundle copying is not board activation. |

Pinned source identities for later imports:

| Source | Commit | Original author |
|---|---|---|
| Runtime stack | `2cd87412cf4684e7d7e0be9f0e50e19b7ec48eb3` | cyw2503 |
| I2C/SPI foundation | `4c3ca9310499a7d73a740267f4a74e7f3daf210b` | jx03-9028 |
| BMP388 increment | `1c0ab7bf2991ee68e49838a7b541c87104da6aba` | jx03-9028 |
| OLED stack tip | `56f718e825674c70716f52d798e5bf011ebaa9f0` | jx03-9028 |
| UART/GPIO | `974eb9c2e3792a1be70a64a4ba7fa9c322ba150d` | kimNewpsysk1t |
| Configurator | `bab09246a477dbedef36a55dad7cd319bac1c1ab` | CharmTzy |

The OLED stack contains the I2C/SPI and BMP388 work. Review that stack once;
do not apply its predecessor branches again. Refresh and compare upstream
before importing if credentials become available. Preserve vendor licensing
and record each imported source revision beside the affected module.

## Implementation order and acceptance

| Checkpoint | Small implementation boundary | Evidence required |
|---|---|---|
| 1. Baseline and shared contracts | This inventory, simple copied samples, explicit ownership and resource ledger | Snapshot verified; sample layouts fit queues; existing baseline tests preserved |
| 2. Target activation | One lifecycle owner, whole-plan replacement, stopped-worker cleanup, activation barrier, queue/health ownership | Board fixture reconfigures independent instances; failures retain ownership and recover or enter diagnosed recovery |
| 3. One real I2C path | Import/adapt the I2C layer and BMP388 wrapper; add the C3 backend and registry adapter | Configured read produces a typed sample; timeout and disconnect are visible |
| 4. Remaining protocol modules | SPI/OLED, UART/LD2450, then GPIO distance and output/PWM | Each protocol works alone, then concurrently within the board limits; alternate profiles reuse the same firmware |
| 5. Configuration transfer and storage | Bounded parser, one USB request at a time, target admission and last-known-good storage | Invalid/interrupted requests preserve the working state; reboot and interrupted save recover predictably |
| 6. Local configurator | Adapt the existing UI to the shared planner and USB client | Manual review, deployment result and live readings work through the browser |
| 7. Generic behaviour | Small trigger/condition/action rules with hysteresis, cooldown and stale-input handling | A configured sensor controls a configured output; alternative routing requires no device-specific runtime edits |
| 8. Platform acceptance | Combined faults, repeated updates, power interruption and resource measurements | Reproducible demo, measured limits and hardware evidence; AI gate can then be reviewed |
| 9. AI | Capability-based contract, training, compact inference and model updates | Recommendations use the same admission/deployment path; model cost fits measured remaining resources |

Current software progress is through checkpoints 6c and 7; see the final
checkpoint record below. Physical acceptance is next, with the team.
Physical peripheral checks are collected for checkpoint 8 at the user's request.
Use whole-plan replacement initially; incremental
per-device transactions, plugin systems and a new web framework are unnecessary.
Keep board/kernel access inside their adapters and the runtime independent of
driver names. Do not advertise a target driver until its implementation is
compiled and accepted for the intended use.

Checkpoint 7 completes the broader workload's sensor-to-actuator requirement
with schema 2.1, while retaining existing 2.0 sensor/display profiles. The first
prototype remains an evaluation example, not the platform's hardware limit.

## Inputs and review boundaries

No new member specification was needed for checkpoint 4a software. Before final
BMP388 hardware acceptance, confirm its actual breakout module, SDA/SCL pins,
supply arrangement and selected I2C address. Before each later peripheral test,
confirm its
actual module variant, pin wiring, supply arrangement and address. A second
physical sensor and an output/actuator module are needed for those hardware
demonstrations. Report unavailable hardware as unqualified, not simulated proof.
OLED also needs controller, dimensions, power and CS/DC/reset wiring confirmed.
These inputs do not block further software checkpoints.

At each checkpoint record affected files, BARR-C/manual review, relevant tests,
hardware evidence where applicable, unresolved inputs and the next small step.
Keep changes local for review; commits, pushes and board flashing are separate
actions. New C must use the existing standard gate without broadening exceptions.
Peripheral integration starts at checkpoint 3; AI remains checkpoint 9.

## Checkpoint 4b: UART/LD2450 software (hardware deferred)

Added `uartio`, the existing-kernel `c3uart` wrapper, `uartfake`, the adapted
streaming parser, bounded radar driver and registry/queue/health adapter.
Each poll handles at most 64 bytes; noise cannot defer report timeout. Native
UART ownership is exclusive; failed setup/cleanup retains diagnosed ownership.
No device-specific dispatch was added to the generic runtime.

Validation: 113 owned C/header files pass the mandatory gate, and 115 host tests
pass (54 checker + 26 foundation + 18 runtime + 17 target). The target build
records 59 inputs and both unchanged, explicitly applied kernel patch hashes.
See [fresh evidence](firmware/qualification/esp32c3-uart-20260928.json) and
[manual review/binding limits](firmware/drivers/hlk_ld2450/INTEGRATION.md).
The pre-checkpoint 249-file snapshot and unchanged Git index are retained under
`%TEMP%/uthome-uart-yjdkni5h/`; the actual dependency checkout stays pristine.

This is software completion only: no flash, reset or peripheral operation.
Native UART timing and the 512-byte resource-patched startup still require
final hardware acceptance. The target-pending manifest remains empty. Next is
GPIO pulse measurement/HC-SR04 and PWM/output software; AI stays last.
No new member input is required for that software work. Confirm actual radar
module, supply/ground, UART wiring and baud before final physical testing.

## Checkpoint 4c: GPIO distance/PWM software (hardware deferred)

Added reusable pulse/PWM contracts, C3 RMT capture and independent LEDC timers,
digital input, portable HC-SR04, typed distance queue/health integration and a
generic PWM component. Pin claims share the existing ledger; failed cleanup
retains ownership. No driver names were added to the runtime or planner.
See [manual review and limits](firmware/drivers/hc_sr04/INTEGRATION.md) and
[fresh evidence](firmware/qualification/esp32c3-gpio-20260928.json).

The mandatory gate covers 132 owned C/H files. All four host suites and the real
C3 build are required for this checkpoint. There was no flash/reset or physical
I/O. C3 capture supports two pairs; PWM supports four independent timers, all
subject to available pins. Arbitrary GPIO interrupt callbacks, live target
profile binding, output routing and physical acceptance remain pending.
Next is checkpoint 5: bounded configuration transfer, target admission and
last-known-good storage. AI stays last and target capabilities remain empty.

## Checkpoint 5a: configuration transfer/storage core (software)

Implemented `cfgbytes`, `cfgwire`, `cfgstore` and `cfgctrl`: one bounded request,
CRC/explicit byte layouts, monotonic IDs/revisions, exact duplicate handling,
required independent validation before activation, distinct active/saved state,
and two-slot readback/commit recovery. Added the matching Python wire codec.
The modules have no driver names, SDK dependency or heap allocation.

Host tests inject failures at every storage operation and torn write prefixes,
check boot/retry/recovery ordering and cross-check C/Python bytes. The mandatory
gate covers 142 C/H files; target compilation uses C99. See
[review and limitations](firmware/src/config/TRANSFER.md) and
[fresh evidence](firmware/qualification/esp32c3-config-core-20260928.json).
No hardware I/O, flash/reset, commit or push was performed.

The next review boundary is 5b: managed-cJSON bounds, semantic/resource admission
and concrete target binding/activation. Then connect physical USB and storage
adapters and test the complete path; this core alone is not deployment. Runtime
and board context lifetime, pin ownership and measured memory remain constraints.
No new member input blocks software. Physical tests remain checkpoint 8; AI is last.

## Checkpoint 5b: bounded admission and sensor binding (software)

Implemented independent parsing/admission, copied settings, BMP388/LD2450/
HC-SR04 production callbacks, shared-resource hooks and explicit empty recovery.
See [contracts, review and exact limits](firmware/src/config/ADMISSION.md).

OLED rendering and PWM command routing remain service/behaviour work; the
binder rejects them. Target restrictions include canonical integer tokens,
no GPIO2 SPI CS and two pulse pairs. The prototype profile is not deployable.
Target advertised capabilities remain empty; no board I/O was performed.

Next, checkpoint 5c connects real USB and isolated persistent storage through
these callbacks. Then complete display/telemetry/local UI and actuator
behaviour before physical acceptance. No new member input blocks software.
AI stays last.

## Checkpoint 5c: USB and persistent-storage application (software)

The bounded transport pump, native C3 USB FIFO adapter, isolated two-slot flash
partition and application entry mode now connect the existing controller,
admission, binding and activation modules. A small serial client probes sequence
state and queries active/saved revisions before explicit changes. It never
automatically retries an uncertain mutation. See
[contracts, review and acceptance limits](firmware/src/config/USB_STORAGE.md).

The runtime remains generic. Target capability advertising remains conservative
until hardware acceptance. No board access, flash/reset, commit or push occurs
at this checkpoint. No new teammate input blocks the next software work.

Four parts remain after this checkpoint, with AI last:

1. **6: services and local application** — telemetry/health reporting, OLED
   rendering through generic sample types, and the local configuration UI.
2. **7: generic behavior and outputs** — bounded rules, typed actuator commands
   and safe routing to the generic output component.
3. **8: acceptance and AI readiness** — physical wiring/driver checks, combined
   load, disconnect/reboot/power-loss recovery, resource/latency measurements,
   and the platform readiness review.
4. **9: AI** — data pipeline, model training/evaluation, lightweight embedded
   inference and integration with the local application.

These are project parts, not a promise of one turn each. Physical inputs remain
the module models, wiring/supplies and available second sensor/actuator already
listed above; confirm them at checkpoint 8. Stop here for the user's review.

## Checkpoint 6a: telemetry and health software

The shared per-source cache, owner-loop heartbeat/health service, read-only USB
snapshot and local JSON-printing client now form one path. They use common
sample types and copied configuration; monitoring does not steal queued data
from a display or rule. See [contracts and review](firmware/src/services/TELEMETRY.md).

Checkpoint 6b now completes [configured OLED rendering and binding](firmware/src/services/DISPLAY.md).
Four review parts remain, including AI:

1. **6c:** local configuration/monitoring application and capability presentation.
2. **7:** generic rules, typed output routing and safe actuator behavior.
3. **8:** physical acceptance, combined-load/resource measurements and readiness.
4. **9:** AI pipeline, model work and integration, last.

At the end of 6b, the next checkpoint was 6c. No new member specification blocks software;
physical module/wiring inputs remain deferred. No board access, flash/reset, commit or push. Stop after this
checkpoint for review.

## Checkpoints 6c and 7: local application and generic rules (software)

Plan check at 6c: the browser uses the shared planner and real USB client for
review, state/capability discovery, explicit deployment and typed monitoring.
It adapts Member 5's small server design; no duplicate validator, new framework
or filesystem deployment workflow is introduced. See [usage and attribution](tools/configurator/README.md).

Plan check at 7: a small pure evaluator and rule-admission module feed the
existing PWM component. Hysteresis, cooldown and unavailable/stale-input zero
handling operate on generic sample types and exact instance IDs. A host fixture
changes a distance/OLED/output profile to environment/output routing in the same
binary. No new device branches are added to the runtime. See [contract and manual
review](firmware/src/automation/README.md).

Software verification and both real build hashes are recorded in
[fresh evidence](firmware/qualification/esp32c3-ui-rules-20260928.json).
The target-pending manifest stays empty; compiled support is not hardware
qualification. No board operation, flash/reset, commit or push was performed.
The preceding 374-file snapshot/index and checked copy remain in
`%TEMP%/uthome-ui-rules-1hjzgz93/`.

**Two project parts remain:** checkpoint 8 (hardware and combined fault/resource
acceptance with the team), then checkpoint 9 (AI). Both are deferred by the
user. Before 8, confirm each actual module, pins, supply/logic levels, the second
sensor and actuator/driver stage; these inputs did not block 6c/7 software.
Stop after this combined checkpoint for review.

## Discovery before AI: current extension (2026-10-12)

The readiness review found the active software checks passing. Existing BMP388,
OLED and radar observations support continued work, but do not complete radar
disconnect/fault acceptance, GPIO/output acceptance or combined timing/memory
measurement. Offline discovery design can proceed independently.

The objective is to observe supported devices on reviewed connections and
resolve protocol/settings from evidence and the catalogue. Keep the generic
runtime and manual configuration path. Do not train a model to guess electrical
facts or claim that every device can identify itself.

| Checkpoint | Deliverable | Status |
|---|---|---|
| D1 | Shared record design, versioned identification policies, descriptor/source consistency checks | Implemented for review; data/design only |
| D2 | Reviewed I2C preset, generic raw observations, USB exchange and PC catalogue matching; BMP388 first rule | Software implemented; physical discovery acceptance pending |
| D3 | Local discovery review, sourced protocol/settings, ordinary node candidate through existing admission/apply | Software implemented; physical discovery acceptance pending |
| D4 | Receive-only radar candidates and manual OLED/GPIO confirmation | Pending |
| 9 | Capability-based training data, rules comparison, small model and measured deployment | After discovery workflow |

See [the detailed contract](docs/DEVICE_DISCOVERY.md). D1 introduces no C changes,
board operations or executable detection logic. Its checks are
`python -B -m unittest discover -s tests/discovery -v`; CI runs them alongside
the existing suites. Future C work retains the mandatory BARR-C gate, manual
review, affected tests and applicable target builds. Review each checkpoint
before continuing. No new teammate specification blocks D1.

D1 validation on 2026-10-12: seven discovery-contract tests passed, including
all checked-in node examples through the unchanged host planner. The 23 legacy
AI regression tests also passed; they remain historical-contract checks only.
`git diff --check` passed. No C changed, so no new target build or hardware
acceptance is claimed. Existing `CONTRIBUTING.md` bench edits were preserved.

D2 keeps model identification out of the embedded observer. The firmware reads
only compiled, reviewed endpoints while the hardware configuration is empty;
the PC matches returned evidence against versioned descriptors. Unknown values
and multiple matches remain explicit. Failed cleanup retains bus ownership and
uses the existing recovery path. No node schema, vendor dependency or BARR-C
exception changes. See [implementation and manual review](firmware/src/discovery/README.md).

Plan check: D3 is next, adding the local review and sourced setting values through
existing node admission. D4 then adds radar observations/manual confirmation;
model training remains last. No new teammate input blocks D3. Physical discovery
acceptance is separate from passing host checks and target compilation.

D2 validation on 2026-10-12: all 188 tests passed across the C-standard (54),
foundation (26), runtime (18), target (54), discovery (13) and legacy AI (23)
suites. The affected target/discovery suites were rerun after the SDK boundary
fix and passed. The mandatory BARR-C gate checked 186 C/H files with zero
findings/errors; manual review is recorded. Both application and acceptance
fixture builds passed with 53 owned compiler profiles checked per build.
See [fresh evidence and build records](firmware/qualification/esp32c3-discovery-20261012.json).

The build also exposed Windows checkout line endings in existing patches and
the cJSON licence. Scoped `.gitattributes` entries now preserve their committed
LF bytes; vendor content, versions and standard exceptions are unchanged.
No board access, flash, commit or push occurred. Existing `CONTRIBUTING.md`
bench edits were preserved. Stop at D2 for review before implementing D3.

## Discovery checkpoint D3: local review (2026-10-12)

Implemented the approved additions within Member 5's existing Configuration
Desk. The discovery card reuses its tiles/icons, fonts, colours and light/dark
themes. The original toolkit under `legacy/member5` remains untouched.

The reviewed I2C connection requires wiring confirmation and explicit detection.
Results separate observations, identity and compiled support. Settings show their
source: descriptor defaults, observed address, reviewed connection or documented
bench starting values. Preset 0.2.0 adds BMP388 timing recommendations; its wire
code, pins and permitted reads are unchanged. Unknown/ambiguous/unsupported
results cannot add a device. This remains a catalogue-based flow, not AI.

Add to profile consumes a short-lived detection token and constructs an ordinary
draft, preserving existing entries and rejecting conflicts. Shared admission
checks the whole candidate. Apply still needs the existing explicit review token
and state/capability checks. Busy detection never stops configured hardware.
Recover now reflects the firmware's actual recovery-required condition. The
local server backlog was increased to handle concurrent style/font requests;
there is no new web framework, dependency, firmware change or standard exception.

Validation: the full target host suite passed 67 tests, including the mandatory
BARR-C gate (186 files, zero findings/tool errors). A subsequent mixed SPI/I2C
draft regression caught and fixed an interface-field assumption; the final
focused runs passed all 13 discovery-UI, 11 configurator/HTTP, 13 discovery-contract
and 5 Node presentation tests. An isolated headless Edge walkthrough with a fake
USB client verified confirmation, detection, sourced settings, preserved draft
entries, review, simulated apply, disconnect and disabled healthy-board recovery.
Both themes were inspected at 1366px and 390px; all assets loaded and no browser
exceptions or mobile horizontal overflow were observed. No hardware access or
new target-build qualification is claimed. Existing D2 build records are preserved.

Plan check: D3 software is complete for review. D4 adds receive-only radar
candidates and explicit manual OLED/GPIO identification; model training remains
later. Physical discovery and the remaining checkpoint-8 acceptance are still
pending. No new teammate specification blocked D3. Stop here for review before
implementing D4. See [the UI guide](tools/configurator/README.md#device-discovery-d3)
for the evidence lifetime, reset limitation and idle-board procedure.

## Discovery checkpoint D4: radar and manual confirmation (2026-10-12)

Implemented the user-approved additions in the existing Configuration Desk,
preserving Member 5's style and toolkit. Radar uses one bounded receive-only
UART window and the existing parser/backend. A clean compatible stream is a
candidate until the user confirms the model; empty/zero-target/noisy/error cases
are distinct. The firmware reports evidence, while the PC catalogue resolves
identity and settings. Exclusive ownership and failed-close recovery are retained.

OLED, HC-SR04 and PWM use explicit module/wiring confirmation without peripheral
test commands. OLED selects an existing compatible readings queue. The six-pin
OLED preset reserves GPIO4 and requires sole use of its SPI bus. PWM is added
disabled and requires schema 2.1.0 plus a separately reviewed rule before enabling.
All additions preserve existing draft entries, use whole-profile admission and
still require separate review/apply. Confirmation cannot extend the original
60-second expiry. Missing drivers, stale evidence, invalid sources and conflicting
resources remain blocking errors. No new framework or dependency is introduced.

Validation: 76 target host tests, 19 discovery tests and 7 Node presentation tests
passed. The mandatory BARR-C gate checked 190 owned C/H files with zero findings
or tool/policy errors; the manual review records no new deviation. Fresh C3
application and acceptance-fixture builds passed, with 54 owned compiler profiles
checked in each. Both staged snapshots match their source manifests. A simulated
Edge walkthrough passed radar confirmation, OLED queue selection, disabled PWM,
separate review/apply and disconnect in both themes at 1366px/390px, with no
script/resource errors or horizontal overflow. Earlier D2/D3 records are preserved.
See [D4 evidence](firmware/qualification/esp32c3-discovery-d4-20261012.json) and
[usage](tools/configurator/README.md#radar-and-manual-devices-d4).

Plan check: D4 software is complete for review. Physical discovery acceptance
and the remaining checkpoint-8 tests are still pending. The next AI step is
collecting measured scenarios and comparing timing recommendations with defaults;
known device identity, protocol, wiring and driver support remain deterministic.
No new teammate specification blocked D4. Stop here for review; no board access,
flash, commit, push or model training was performed.

## Training checkpoint T1: offline timing experiments (2026-10-13)

Implemented `python -B -m ai.timing` to prepare, validate and export interval
experiments using current platform admission. Each plan targets one explicit
instance and compares supplied intervals with its current setting and descriptor
default. Full-profile validation preserves other instances and rejects invalid
limits, missing drivers and resource conflicts. Capability lookup does not use
the historical four-device AI enum. Stream intervals and actuator tuning are
outside this scope; rule-controlled outputs must already be disabled in the
input profile.

The versioned scenario names a goal, conditions, family, observation duration and
repetitions. Plans retain profile/build inputs, source versions/hashes and trial
profile hashes. Revalidation rejects changed reviewed sources. Output files are
created exclusively, and exported profiles still require the existing connected
review/apply flow. Plans explicitly contain no identity evidence, actual firmware
hash, measurements or training labels. The included example uses the two-sensor
host fixture and is not today's bench wiring. See
[TIMING_EXPERIMENTS.md](ai/TIMING_EXPERIMENTS.md).

Validation: 15 new timing-preparation tests, 23 retained AI contract tests and
19 discovery tests passed (57 total). Coverage includes independent instances,
capability-based descriptor extension, full-profile rejection, unchanged fields,
source drift, fabricated measurement claims and CLI round trips without file
overwrites. CI now runs the combined AI suite. No C/H, firmware, UI or vendor
implementation changed in T1; the prior BARR-C and target-build evidence remains
historical rather than being restated as a new qualification.

Plan check: D1-D4 software enables this offline preparation; physical discovery
and remaining checkpoint-8 acceptance are still pending. T2 will bind measured
runs to the actual firmware/profile and device evidence, including interrupted
run handling. Agree the bench arrangement and freshness/visible-delay goal before
collection. T3 will compare outcomes with defaults/simple rules and define the
new model contract before training. No new teammate input was required for T1.
Stop here for review. No board access, deployment, commit, push or training occurred.

## Equipment-free AI pipeline: T2/T3 and C preparation (2026-10-13)

The user has no equipment available and authorized all useful offline model
work. Implemented run-file replay and validation, a deterministic simulator,
dataset labels from complete repetitions, grouped training/validation/test
splits, a pinned small decision tree, comparisons with existing/default/simple
rules, experimental recommendations and checked C export. Device identification,
protocols, electrical facts and driver availability remain deterministic.
No UI adjustment or connection to the existing Apply flow was made.

The namespaced timing contracts are version 0.1.0, independent of prototype AI
0.2.0. Run records bind experiments, intervals, repetitions, sessions and profiles.
Observed resets, changed configurations, missing windows and incomplete
repetitions cannot create positive training labels. Complete failures remain
negative examples. Hardware and simulated origins cannot be mixed. Hardware
metadata is a declared reference requiring original evidence, not proof by itself.
Live serial collection remains unimplemented; file replay can be exercised now.

The default rehearsal produced 48 simulated scenarios in 20 families, 576 runs
and 192 candidate examples. Related goals/repetitions stay in the same family;
identical generated profile families are merged. The final tree has 21 nodes.
On ten held-out scenarios it chose the longest tested passing interval eight
times, with one goal violation. The simple rule achieved nine such choices,
also with one violation. There is no demonstrated reason to replace the rule.
All models/recommendations remain non-deployable, and synthetic predictions need
explicit offline opt-in. No hardware performance or energy benefit is claimed.

Validation: all 50 AI tests and 19 discovery tests passed (69 total). The mandatory
BARR-C gate checked 194 owned C/H files with zero findings/tool errors. Generated
C received explicit checks and matched Python for 1,248 vectors. Host and C3
object compilation used C99 with warnings as errors. The standalone C3 object
contains 584 bytes of code/read-only data, no writable static data and a compiler
estimate of zero local stack bytes for the optimized function; this excludes
caller/integration overhead and is not linked firmware or measured board use.
The manual review is in [OFFLINE_PIPELINE.md](ai/OFFLINE_PIPELINE.md#c-review).
No new C deviation, exception, SDK change or firmware adapter change was needed.

Plan check: the equipment-free preparation/training/export path is complete.
Physical D2/D4 discovery and checkpoint-8 acceptance remain pending. Next steps
with hardware are actual capture, measured dataset collection, retraining and
untouched held-out evaluation. Firmware integration and measured timing/resource
qualification follow only if there is a useful advantage over simple rules.
No new teammate specification blocked this work. See
[the repeatable guide](ai/OFFLINE_PIPELINE.md) and
[evidence record](ai/qualification/offline-20261013.json).
No board access, flash, commit or push occurred.
