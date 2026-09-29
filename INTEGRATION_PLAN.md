# Platform integration checkpoints

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
