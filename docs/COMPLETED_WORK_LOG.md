# Completed work and superseded checkpoint log

Archived during the documentation sweep on 27 October 2026 from the published
integration checkpoint `bc94134`. This is a historical log, **not current task
status or instructions**. Phrases such as "next", "pending", "current" and old
branch/board identities below describe their original checkpoints. Current work
is in the [root plan](../README.md#remaining-work); technical contracts and useful
lessons remain in the maintained guides. Original JSON evidence, raw logs,
approvals and source hashes are unchanged. Historical paragraph links identify
their source files; renamed section anchors are not evidence changes.

## 1. Root checkpoint chronology and repeated progress tables

Source: [README.md](../README.md).

#### Resume next session

The [overnight handoff](../firmware/qualification/system-overnight-handoff-20261027.json)
records a working **USB static core**, completed AI collection and separate
on-device radio diagnostics. All eight phases have progressed; the complete
wireless, no-malloc, AI-assisted product is **not release-qualified**.
Use the [overall delivery plan](../README.md) for the remaining gates.

**At the overnight handoff, serial was closed and the original three-peripheral
profile was restored.** Close any current Desk/serial client before bench work.
The retained application ELF is
`597ae600b7377256f7dcf23978e4c930cd2c9d1036ee5b8bf0471279134ee801`
(`app-cpu-a1-archive`), active/saved revision **737**, CRC
**3623560383** at the final check. Enumerate and verify again before use.
The [scheduler/core acceptance](../firmware/qualification/esp32c3-scheduler-20261027.json)
passed two 1,699-check fixture boots, twenty profile/discovery cycles, failed-
activation rollback, six capacity/error cases, host disconnect and saved-profile
reboot. Its fixed internal-RAM footprint is **230,152 bytes**. Application startup
and runtime heap counters and post-startup native creation counters stayed zero
in the observed core scope. Two stability captures total **45 minutes** and
**3996 paired observations**, with fresh BMP388/radar
readings throughout. Physical OLED confirmation of this image remains deferred.
Scheduler percentages are sampled native non-idle time, not cycle/ISR accounting;
worker stack margins are measured after quiescence, not scanned live.

**AI collection is complete: 272 usable runs and 99,552 observations.** The
[primary study](../ai/qualification/protocol-static-study-20261027.json) has 192 runs;
the [corrected stress study](../ai/qualification/protocol-stress-study-20261027.json)
has 80. Both frozen models fail the unchanged benefit gate and remain experimental.
The primary model correctly abstains on three unsafe held-out choices; the stress
model matches the simple rule. Neither is enabled for normal configuration.
The [isolated target probe](../ai/qualification/model-target-probe-20261027.json)
passed 1,165 parity vectors on each of two boots; its largest observed four-
candidate batch was 83 microseconds, with no counter changes. This does not prove
loaded inference or new-device generalization. See the
[training-only design review](../ai/qualification/primary-training-design-review-20261027.json)
before planning a new varied-goal/protocol-cost study with an analytical baseline.

The study exposed an SPI polling delay. The
[controlled correction](../firmware/qualification/esp32c3-spi-polling-20261027.json)
passes 500 kHz through 4 MHz; 400 kHz still exceeds the unchanged 20 ms frame
deadline. Negative data and the unexecuted/rejected earlier study plans are
preserved. The two studies use different frozen firmware contexts and must not
be silently merged or presented as wireless-load or unseen-peripheral evidence.

**Wi-Fi scan/stop/restart passed on the ESP under uT-Kernel.** The
[before/after comparison](../firmware/qualification/esp32c3-radio-diagnostics-20261027.json)
isolated the rejected 200-by-8-byte queue; repartitioning the same 8,192-byte pool
allowed all 25 observed requests and scans finding 14/18 access points. BLE
advertising was observed by the PC, and the separate
[read-only GATT static-host diagnostic](../firmware/qualification/esp32c3-gatt-settle-20261027.json)
**passed**. These diagnostics retain SDK heap allocations and do not qualify
static-compliant networking, authenticated services, MQTT/HTTP or coexistence.
The [source audit](../firmware/qualification/esp32c3-wireless-source-audit-20261027.json)
identifies the remaining task, notification, timer and SDK allocation work.
No credentials or external network association were used. Configuration flash
was preserved and the normal USB image restored after radio tests.
The [first GATT attempt](../firmware/qualification/esp32c3-gatt-20261027.json) remains
recorded separately: Windows saw the advertisement but could not open the device.
An [active-scan/public-address retry](../firmware/qualification/esp32c3-gatt-retry-20261027.json)
and the [initial static-host run](../firmware/qualification/esp32c3-gatt-static-20261027.json)
retain their separate outcomes; no failed record is rewritten. The last test uses
Windows device access from the start of a fresh diagnostic connection window.
The [short-disconnect test](../firmware/qualification/esp32c3-gatt-access-20261027.json)
read four changing values and rejected writes, but Windows reused one physical
link across two client sessions. The final test allows eight seconds between
sessions and still requires the board's two-connection gate; the gate is unchanged.

The latest [software/build checks](../firmware/qualification/esp32c3-static-host-build-20261027.json)
pass 183 target tests, the 283-file pinned C gate and fresh isolated/ordinary
builds. The [broader regression](../firmware/qualification/system-software-regression-20261027.json)
also records 119 AI tests and policy/foundation/runtime/discovery/dashboard checks.
The latest ordinary source build (`app-static-host-isolated-a1-archive`) excludes radio
and model entry points; it is build evidence, while the installed image above
retains its own completed bench acceptance. Pinned SDK/kernel checkouts are pristine.
The Windows parser-test crash and HTTP rejection connection-reset issue are fixed.
No commits or pushes were made during the overnight run. The shared branch
contains source and qualification summaries; the raw captures, build archives
and board backup listed below remain local and are not included in Git.

The fixed adviser controller and typed feature builder are host-tested, and a
concrete [Desk proposal](../ai/README.md)
is ready for review. Member 5's visual style is unchanged. Next autonomous software
work is static wireless infrastructure and bounded authenticated services, then
combined qualification, a useful prospectively evaluated model and loaded advice
integration. When available, the user can confirm the OLED, review the proposed
Desk controls, supply network setup through an appropriate local configuration
path, and assist with additional peripheral/output and controlled power-loss tests.
The radar zero-target check remains skipped at the user's request.

Preserve `build/system-completion-20261026-233938/`: scripts, raw captures,
immutable builds and the verified **4 MiB board backup** are there. Preserve
`build/protocol-study-20261027-static-r2/`,
`build/protocol-study-20261027-stress-r3/` and earlier rejected plans separately.
The earlier `overnight-soak-a9/` contains 7,199 observations over 30 minutes;
stability captures are not additional labelled AI training runs. The bounded
keep-awake helper is stopped at handoff; no persistent power setting was changed.

##### Earlier checkpoints

Hardware work resumed on **26 October 2026**. Before the branch update below,
the checkout was `main` at `c46c2f7`. Check `git status` for local edits before
continuing.

The working checkout is now **`system-integration` at `f1234d3`**, tracking
`origin/system-integration`, with local AI/bench/runtime work reapplied. The
14 incoming commits add permanent HAL synchronization, retained native UART
ownership/static records, optional output modules and CI/build fixes. Local
changes remain uncommitted; the recovery stash is
`5ee4f4d1fcd62db4e37a986a103b6fac3650d2dd` and the verified file backup is
`build/integration-sync-20261026-135119/`. No board was flashed by this update.
The [integration check](../firmware/qualification/esp32c3-system-integration-20261026.json)
passed 218 target/runtime/discovery/AI tests, the 221-file C standard gate and
fresh application/fixture builds. Both source snapshots match the combined
working tree. These results are software/build evidence only.

The subsequent [Member 4 implementation](../ai/qualification/static-adaptation-20261026.json)
adds static execution contracts, capture gates, fixed measurement helpers and
the eighteen-input protocol C exporter. The integration application is now
installed after a verified 4 MiB backup. Its
[discovery regression](../firmware/qualification/esp32c3-integration-bench-20261026.json)
passed twenty cycles, forty successful empty/original Apply operations, twenty
target capacity rejections, and a separate failed-activation/rollback test.
The original profile is active and saved at revision **179**, CRC **3623560383**;
BMP388/radar readings are fresh and the user confirmed the OLED updating.
Serial is closed. Preserve `build/static-ai-20261026/` and its external backup.

- The user is **Member 4**, but has now authorized taking over **all remaining
  system work**, including runtime, configuration, drivers, observability and
  integration. Follow the [overall delivery plan](../README.md);
  former member boundaries are provenance, not reasons to wait for another
  teammate. Keep the platform configurable through supported drivers/profiles.
  Preserve Member 5's UI and present proposed UI changes before implementing them.
- **Mandatory new priority:** the supplied static-memory requirement prohibits
  application heap use and kernel-object creation after startup. The
  [impact review](../firmware/README.md) found parser,
  worker/rollback, HAL-lock, native-UART and SDK-storage changes are required.
  The fetched branch implements the HAL/native-UART portions in source; fixed
  workers and fixed parsing/storage are now implemented. Complete the remaining
  allocation/object/stack and combined wireless verification before qualifying another model for
  deployment. Older bench results retain their pre-integration image identities.
- The group's tribunal discussion specifies four permanent workers, initially
  8 KiB each, fixed parser storage and startup-created protocol locks. The
  [AI adaptation plan](../ai/README.md)
  maps those decisions to discovery, resource contracts, fresh measurements and
  bounded controller-side inference. Contracts/export preparation are implemented;
  measurement runtime/telemetry integration now has USB-core evidence; complete
  wireless/static qualification remains pending. The teammate HAL changes retain our cleanup-owner fix.
- BMP388 (I2C), six-pin OLED (SPI) and LD2450 (UART) are assembled and satisfy the
  protocol minimum. HC-SR04, resistors, LEDs and a multimeter are also available.
  Keep jumper wiring intact where practical. Enumerate and identify the ESP
  afresh; neither the last COM port nor the installed image is an assumption.
- The [cleanup correction](../firmware/qualification/esp32c3-cleanup-20261026.json)
  restored profile replacement and physical I2C/UART discovery. A subsequent
  [shared-guard correction](../firmware/qualification/esp32c3-shared-handoff-20261026.json)
  addresses completed samples and heartbeats being lost when a task preempts
  another holder of the shared-data guard. It passed 895 fixture checks on each
  of two boots and was installed after a verified 4 MiB backup. Configuration
  flash remained byte-identical. The ten-minute combined capture observed all
  4,380 BMP388/radar snapshots fresh, all 1,200 BMP attempts published and no new
  device/UART errors or missed heartbeats. The user confirmed the OLED readable
  and updating. That checkpoint retained revision 87 / CRC 3623560383; the later
  AI study below restored the same settings at revision 138.
- The user **deferred further radar zero-target checks**. Absence acceptance
  remains open; do not resume it without a new request. The earlier intermittent
  UART failure and its cause remain unresolved. Neither the phone-app comparison
  nor the proposed upstream 50-byte-ring explanation established a cause. See
  [bench evidence](../firmware/qualification/README.md)
  for the retained observations, photos-based wiring interpretation and limits.
  Hardware qualification remains false.
- The [live recorder and protocol study](../ai/README.md)
  are implemented. The [first measured study](../ai/qualification/protocol-study-20261026.json)
  contains 96 runs, 24,576 observations and a nine-node host-only tree. It abstained
  on both test families and made one failing validation recommendation; useful
  generalization is unproven. Preserve both `build/protocol-study-20261026-r1/`
  and `r2/`, their frozen sources and model. They describe the **older firmware**.
  Do not relabel those runs or tune on their test outcomes. No model was retrained
  or deployed during the runtime correction.
- The [post-fix study](../ai/qualification/protocol-study-shared-20261026.json) is
  complete: 96 fresh runs, 24,576 observations, 48 labelled candidates and a
  seven-node host model. Four evaluation workloads were reserved before capture
  and were absent from the first study. It made no wrong recommendations but
  unnecessarily declined the passing 2 MHz SPI workload; its predeclared coverage
  criterion failed. The [decision analysis](../ai/qualification/protocol-analysis-shared-20261026.json)
  records the learned 2.5 MHz boundary. No model was deployed or UI integrated.
  Artifacts/frozen sources are under `build/protocol-study-20261026-r3/`, with
  completed recordings in `capture-verified/` and results in `trained/`.
  The first `capture/` is a preserved zero-run checksum preflight abort.
  That study ended at active/saved revision **138**, CRC
  **3623560383**, with the original settings restored; its final query found fresh
  BMP388/radar samples and no runtime errors.
- Next implementation: complete stack and work-cost evidence for the fixed core,
  retaining the observed allocation/object regression as part of the same effort.
  The user now explicitly requires **Wi-Fi and BLE on the ESP**. First audit and
  prove the pinned port's wireless integration; include its infrastructure tasks,
  buffers, object lifetimes and image size before freezing the system budget.
  Wire the tested `workcost` helper
  into that runtime and versioned telemetry, measuring its overhead. The new
  collector CLI requires reviewed execution-context/evidence arguments and
  refuses an unqualified baseline before Apply. The standalone protocol export
  has Python/C parity and C3 object-size evidence, but is not installed.
- Next AI study: characterize transferable execution/transfer costs or expand
  development coverage, then predefine new untouched evaluation groups before
  revising the model. Do not tune its threshold to the revealed test outcomes.
  Unknown or unsupported
  contexts must retain normal configuration through abstention. A held-out device
  type is still required before claiming cross-device accuracy.
- Read [remaining work](../README.md), [bench evidence](../firmware/qualification/README.md)
  and [local artifacts](README.md).
  Current build snapshots, serial logs, before/after captures and backup reference
  are retained under `build/shared-handoff-20261026/`.

#### Current status

| Area | Status |
|---|---|
| Runtime, HAL, supported drivers, profile transfer/storage, telemetry and rules | Implemented; software and target-build evidence retained |
| Existing local UI and reviewed I2C/UART/manual discovery | Implemented; initial physical BMP388/UART observations verified on 26 October; full discovery acceptance remains open |
| Basic BMP388, OLED and radar operation | Updated firmware: ten-minute combined capture fresh throughout; OLED visibly updating. Earlier intermittent UART failure and wider acceptance remain open |
| Offline AI pipeline | Simulated runs, training, evaluation and C export implemented |
| Live AI data capture | Two completed 96-run studies; latest on corrected firmware with four newly reserved evaluation workloads |
| Protocol AI model | Primary static study: seven nodes, zero held-out violations, three correct abstentions; interval/coverage benefit gate failed |
| Static-memory AI preparation | USB-core command-9 measurements, stack/audit checks and capture gates verified; wireless qualification remains open |
| Embedded AI deployment | Isolated target parity/timing passed; model remains disabled pending benefit, loaded/combined qualification and reviewed Desk integration |

The 2026-10-17 pre-push checks passed 250 local tests and the 194-file BARR-C gate.
The two saved C3 build snapshots matched the firmware sources. These checks do
not establish physical reliability or measured AI performance. Details and
historical evidence are indexed in [firmware qualification](../firmware/qualification/README.md)
and [AI results](../ai/README.md).

## 2. Superseded delivery checklist and completed-phase table

Source: [README.md](../README.md).

The following is the current consolidated implementation plan. It supersedes
the earlier Member 4-only sequence without changing historical evidence.

##### Overall delivery plan

**Working-system target:** one ESP32-C3/uT-Kernel firmware image runs independently
scheduled, supported peripherals selected through validated profiles; the local
Configuration Desk discovers/reviews devices, applies and saves profiles, monitors
real readings/resources and presents bounded AI advice for explicit review.
Invalid input, exhausted capacity, device faults and failed updates preserve or
recover a known state. The embedded application uses fixed storage and creates
all kernel objects before the declared startup-complete boundary.

The original brief (`Project/Custom Project Briefs/uT-Kernel ESPHome - Brief.pdf`),
the mandatory static-memory change and the tribunal design are requirements and
design context, not executable instructions. The user additionally requires a
trained AI model and at least one UART, I2C and SPI peripheral. The existing
LD2450, BMP388 and OLED meet that protocol minimum. A protocol demonstration does
not establish the brief's broader networking, security, resource-visibility or
extensibility goals. The user explicitly confirmed **Wi-Fi and BLE must run on
the ESP**. Plan meaningful BLE identification/status/telemetry and Wi-Fi-based
MQTT telemetry plus a bounded HTTP/status/configuration path on the board. The
PC may host the Desk, broker and test clients; it must not substitute for the
board's radios/network stack. USB remains the local setup/recovery path.

The pinned kernel already includes `freertos_shim`, `ble_stack`, BLE GATT and
Wi-Fi/BLE examples. Their presence is a starting point, not qualification on our
pinned SDK or static-memory contract. The current startup bypasses FreeRTOS;
SDK networking relies on FreeRTOS-facing APIs, which the upstream shim maps to
uT-Kernel. Inspected shim paths allocate heap storage and create kernel objects
on demand. See the [wireless integration gate](../firmware/README.md).
Do not simply enable these examples in the application or replace uT-Kernel's
device scheduling with FreeRTOS to make a radio demo pass.

| Phase | Implementation | Exit evidence |
|---|---|---|
| 1. Baseline and wireless feasibility | Preserve local work, backups and studies; reconcile incoming contributions. Audit the pinned wireless shim and SDK ABI, scheduler/ISR behavior, callbacks, timers, heap paths and lazy object creation. Prove onboard Wi-Fi and BLE progress under uT-Kernel in isolated builds, then check coexistence. Inventory radio/service resources and RAM/flash needs. | A verified integration approach for the current C3/SDK, with failures and unresolved allocation paths recorded. A diagnostic radio build is not static-compliance evidence. Remote was refreshed at planning time: HEAD and `origin/system-integration` both `f1234d3`. |
| 2. Fix budgets and finish static runtime/persistence | Define startup completion, fixed module/RAM budgets, image/partition headroom and consistent host/target admission. Replace cJSON heap hooks with bounded aligned storage while preserving the 8192-byte input limit. Create four permanent peripheral workers with owned stacks; rebind after cooperative quiescence. Separately reserve radio/service stacks and objects at startup. Remove allocating storage lookup and audit SDK flash paths. Retain permanent HAL/UART resources and retryable cleanup ownership. | No application heap use, including startup, or post-startup object creation through Apply/rollback/recovery. Four 8 KiB worker stacks are an initial allocation to measure, not free RAM. Network services do not silently consume peripheral slots. Fresh host tests and application/fixture builds pass. Any required partition change preserves/migrates configuration through the reviewed backup/install path. |
| 3. Bounded onboard communications and measurements | Integrate Wi-Fi, BLE, MQTT and HTTP through fixed service resources and bounded connection/message limits. Reconnect/disconnect/exhaustion must not create application heap allocations or new kernel objects. Serialize transport mutations through the existing controller; enforce authentication and reviewed Apply, protect credentials, and keep local operation independent of connectivity. Integrate `workcost`, per-device errors, reservations, allocation/object counters and stack measurements through versioned telemetry. Establish scheduler/idle accounting before showing CPU utilization. | Wi-Fi/MQTT/HTTP and BLE GATT work on the ESP with measured coexistence and resource use. C/Python wire parity, true active/reserved counts and instrumentation overhead are verified. Any narrowly justified Wi-Fi-driver exemption is identified; it is not a blanket BLE/TCP/IP/TLS/application exemption or permission for late kernel objects. |
| 4. Qualify the integrated platform | Exercise maximum capacity, oversize input, parser/queue/network-buffer exhaustion, discovery BUSY/cleanup failures, twenty Apply cycles, failed activation/rollback/recovery, USB/radio reconnect, saved-profile reboot and controlled interrupted-save behavior. Verify BMP388/OLED/radar with simultaneous Wi-Fi/BLE traffic and HC-SR04/LED/PWM automation in compatible layouts. | Fresh evidence on the static image; zero forbidden allocations/new objects, measured stack/time headroom, correct active/saved state and safe outputs on stale/fault input. Broker/AP/client loss cannot stop local operation. Keep unavailable physical tests explicitly open; do not set a blanket hardware-qualified flag. |
| 5. Fresh AI study and useful model | Use measured transferable costs to decide whether the current eighteen configuration features are sufficient, including wireless/service load. Freeze feature availability, independent calibration, radio/observer load, firmware/resources, candidates, goals, sessions and new whole-family splits before capture. Train on development data and compare with simple rule/current/default settings. | Reproducible journals and grouped evaluation with violations, abstentions, coverage and interval benefit. No test-label tuning or fabricated completion. Evaluate a different supported peripheral type before cross-device claims; unknown contexts must still configure normally through abstention. |
| 6. Embedded adviser and Desk integration | Build the fixed-state controller wrapper from validated typed settings: one pending request, at most four candidates, explicit BUSY/UNSUPPORTED/no-advice outcomes, profile/build/model identity and stale-advice rejection. Link an eligible exported model and add advice/review controls to the existing Desk style after presenting the concrete UI proposal. | C/Python parity, allocation/stack/code limits, loaded request latency and AI-on/off sampling comparison. Advice never applies itself; changing the draft/session/build invalidates it, and ordinary target validation still governs Apply. |
| 7. End-to-end product and extension checks | Demonstrate import/edit -> discovery/manual confirmation -> validation -> Apply/save -> monitoring -> advice -> reviewed change -> restart/restore. Exercise driver/catalogue extension and unsupported-device behavior. Document the actual local/network trust boundary, authentication limits and verified firmware-update procedure. | Supported profile changes require no generic-runtime rewrite; unavailable drivers never appear auto-supported. UI reflects faults and uncertain saves accurately. Communication/security claims match implementation; no unsupported OTA, cloud or universal hot-plug claims. |
| 8. Release evidence and demonstration | Run the complete affected regression matrix and final real-target builds; retain ELF/map/source hashes, fixed-memory table, startup-object inventory, overflow demonstrations, lifecycle/AI results, installation/rollback instructions and a reproducible demo script. Prepare a reviewable repository handoff. | A fresh setup can reproduce the supported demo and evidence. All release claims trace to measured results. Commit/push/publication follow the user's authorization; historical qualification records and teammate attribution remain intact. |

Current progress is scoped by these gates; diagnostic success does not complete
the full phase:

| Phase | Completed evidence / active work | Still required |
|---|---|---|
| 1 | Integration/backup preserved; Wi-Fi scans and stop/restart pass, BLE advertising observed | Association/coexistence and remaining shim semantics |
| 2 | Fixed parser/storage/workers, USB heap/object audit, queue/event core and RAM budgets | Static radio tasks, notifications, timers, BLE/lwIP paths |
| 3 | Work-cost/stack/audit telemetry, scheduler observations and wire parity; read-only GATT diagnostic passed | Integrated static MQTT/HTTP/BLE, authentication and fixed resource limits |
| 4 | USB cycles, rollback, capacity, reboot, host disconnect, SPI regression and 45 minutes of final core stability | Combined radio load and assisted physical/output tests |
| 5 | 272 usable runs / 99,552 observations across two analyzed studies; both benefit gates fail | A prospectively redesigned useful model, independent new-device and wireless-load evidence |
| 6 | Fixed adviser/typed features host-tested; two target parity/timing boots passed | Eligible model, wire integration, loaded AI comparison and reviewed Desk controls |
| 7 | Local workflow/extension rejection tests; HTTP rejection race fixed | Complete network/advice demonstration and physical extension checks |
| 8 | Consolidated immutable handoff, builds/maps/raw data, regression matrix and reproducible USB demo | Final combined-system release qualification and assisted demonstration |

**Execution order:** phases 1-4 produce the compliant core milestone; phases 5-6
produce the measured AI milestone; phases 7-8 close end-to-end acceptance and
submission. Prepare inference interfaces, offline tests and the proposed Desk
flow alongside core work, but do not install a failed model or collect
deployment-qualifying training data on an unqualified baseline. A communication
change affecting firmware, resource use or observation load belongs before the
phase-5 freeze. Core fixes are now implementation tasks, not external teammate
blockers. Fixed parser/storage/workers and USB measurement telemetry now have
scoped evidence. The immediate sequence is the remaining wireless compatibility
and allocation work, fixed service lifecycle, combined inventory and networking.
The USB-only studies remain valid for their exact images; they cannot qualify
models for future wireless load or a different execution context.

**Acceptance boundaries:** retain the existing three-peripheral wiring where
practical. Recheck board identity, image and available equipment before hardware
operations; preserve a verified backup before installing a changed image.
HC-SR04/output checks use the documented alternate layout and require physical
assistance. The radar zero-target check remains explicitly user-deferred; it is
not a prerequisite for healthy transport/timing data. Do not generalize LED/PWM
tests to untested motor/relay power hardware.

The seven-node model's prior coverage failure remains a failed gate. If another
candidate is not useful, record that result, keep normal configuration working
and continue development without claiming the AI milestone complete. A larger
local/API language model is an optional host explanation feature, not a
substitute for measured embedded-model behavior or a prerequisite for this plan.

Every changed owned C module follows the existing BARR-C policy, full pinned
format gate, manual review and relevant warning-as-error tests. Changes to
target adapters/ABI/fixtures require fresh target builds; pins, upstream revisions,
reviewed exceptions and historical source hashes are preserved. UI presentation
changes retain the existing review requirement. Software work can proceed through
these phases without stopping after each one; physical actions, genuinely missing
requirements and mandated UI/deviation reviews remain explicit checkpoints.

## 3. Permanent worker terminology

Source: [firmware/README.md](../firmware/README.md).

`tkport` owns task prepare/start/cooperative stop/delete;
`activate` owns whole-plan replacement. Forced termination is not cleanup.

## 4. Historical kernel-pool size

Source: [firmware/README.md](../firmware/README.md).

disables unused native ADC startup. The retained 26 October application ELF
contains the upstream **32 KiB** `mtk_default_heap`; the earlier guide's 64 KiB
override claim was incorrect. This pool is neither free application RAM nor an
AI budget. Its allocation lifecycle also needs the review below.

## 5. Static-memory impact assessment, migrations and build checkpoints

Source: [firmware/README.md](../firmware/README.md).

#### On-device wireless integration gate

Wi-Fi and BLE must run on the ESP under uT-Kernel. The PC may provide the Desk,
MQTT broker and test clients, but cannot replace the board's radios. The planned
application services are BLE identification/status/telemetry and Wi-Fi MQTT
telemetry plus bounded HTTP status/configuration. USB remains available for local
setup and recovery. These services are **not integrated in the current image**.

The pinned port provides `components/freertos_shim`, `components/ble_stack`,
`examples/ble_gatt` and wireless SDK configuration examples. Its startup wrapper
bypasses the FreeRTOS scheduler; SDK calls need a compatible implementation over
uT-Kernel. Audit the actual pinned SDK and linked call paths before selecting the
integration, rather than treating an upstream example as an accepted application.
Source inspection already identifies these unresolved paths:

| Pinned component | Finding relevant to the mandatory change |
|---|---|
| `freertos_shim/shim_task.c` | Task creation allocates stack memory with `heap_caps_malloc` and creates a notification semaphore. Fixed task-entry storage alone is insufficient. |
| `freertos_shim/shim_queue.c` | Dynamic handles and queue storage use heap allocation; synchronization objects are created during initialization. |
| `freertos_shim/shim_queue.c` ring capacity | The head/tail sentinel consumes one of `len` slots; a length-one queue cannot hold an item. Correct and test the owned compatibility path before using it for network services. |
| `freertos_shim/shim_evtgrp.c` | `__wrap_xEventGroupCreateStatic` ignores the supplied buffer and delegates to the heap-backed creation path. An API name containing `Static` is not evidence of compliance. |
| SDK Wi-Fi, BLE NPL and lwIP adapters | Reach task, queue, semaphore and allocation APIs. Their reachable initialization, traffic and reconnection paths need separate review. |

An owned [fixed queue adapter](../firmware/src/network/README.md) is now under isolated
build/test qualification. It supplies complete one-item capacity, front/back/
overwrite semantics, fixed leases, startup-only synchronization objects and
absolute wait budgets using the SDK tick frequency. It also corrects the
scheduler-state ABI values. It is not selected in the collecting image and does
not yet qualify tasks, event groups, timers, BLE/lwIP allocation or cache-off
radio traffic. The default USB application stays independent of this option.

The pinned BLE GATT and Wi-Fi UDP examples built successfully on 27 October.
Their immutable build records and sizes are indexed in the static-core evidence;
neither example was installed. Their automatic NVS erase paths, open-network demo
settings, heap use and object lifetimes are not accepted product behavior.
Use isolated builds to establish Wi-Fi and BLE progress, then simultaneous
operation with uT-Kernel device workers. Check SDK ABI, stack-size units,
ISR/callback behavior, timers, notification semantics and required shim stubs.
Record unsupported operations explicitly; a successful diagnostic build or radio
connection does not prove correct scheduling or static-memory compliance. Keep
upstream revisions intact and put adaptations in owned wrappers/build integration
subject to the existing C policy. Do not switch device scheduling to FreeRTOS.

Before freezing the application budget, inventory all infrastructure tasks,
stacks, kernel objects and buffers separately from the four peripheral slots.
Select bounded connection, payload and queue limits; precreate the resources
needed for disconnect/reconnect and keep their lifetime independent of clients.
Audit BLE, TCP/IP, MQTT, HTTP, credential storage and any TLS path as well as the
Wi-Fi driver. The permitted, justified Wi-Fi-driver exemption is not a blanket
networking exemption or permission to create kernel objects after startup.
If a pinned dependency cannot meet the contract, record the specific path and
resolve the implementation before claiming compliance.

Measure combined RAM, stack and flash headroom, including the kernel pool and
SDK reservations. Check image fit against the actual partition layout before
selecting networking options; any required partition change needs a verified
backup and configuration-preservation/migration procedure. Do not copy the
upstream demo's open-access configuration or automatic NVS erase fallback into
the product path.

Transport callbacks must pass bounded requests to the existing controller owner.
Retain validation, authentication, explicit reviewed Apply and truthful save/fault
responses across USB and network clients. Protect credentials and keep sampling,
automation and recovery working when AP, broker or clients disappear. Acceptance
must exercise simultaneous radio/peripheral traffic, reconnects, full queues,
oversized messages and client limits while measuring allocations, object counts,
stack headroom and timing. This combined baseline precedes qualified AI capture;
see the [overall delivery plan](../README.md).

#### Mandatory static-memory change

The overall completion scope now also requires **on-device Wi-Fi and BLE**.
Include the [wireless integration gate](../firmware/README.md)
before finalizing this memory design or collecting a new qualified AI study.

**Review status, 26 October 2026: current firmware does not comply.** This is an
impact review, not an implemented migration or new hardware qualification.
After the review, `system-integration` at `f1234d3` was fetched and checked out
with the local fixes retained. It implements startup-created I2C/SPI/UART/pulse/
PWM mutexes, one retained native UART descriptor and 1280 bytes of fixed native
UART-record storage. The table below records the **pre-pull findings**; its HAL
and native-UART items now have source implementations. Parser, worker lifecycle,
SDK storage, whole-system budgeting and full hardware acceptance were still open
at that integration checkpoint. The later static-core changes below supersede
those source gaps, without rewriting the pre-pull findings.
The integration application was subsequently installed after a verified full
backup. Its [bench regression](../firmware/qualification/esp32c3-integration-bench-20261026.json)
passed twenty discovery/profile cycles and a separate failed-activation rollback;
the original settings were restored at revision 179. This does not establish
static-memory compliance.

The 27 October implementation reserves four 8 KiB caller-owned worker stacks,
uses typed fixed cJSON pools (28,672 pool bytes on C3, plus 26 control bytes), and
replaces allocating storage lookup/protection paths with an exact checked layout.
See [configuration](../firmware/src/config/README.md),
[worker lifecycle](../firmware/src/runtime/README.md) and the
[fixed-budget tool](../tools/target/README.md).
The ordinary runtime fixture passed 909 checks on two boots; the new application
passed twenty discovery/profile/save cycles and failed-activation rollback, then
restored revision 260 / CRC 3623560383 after reset with fresh BMP388/radar readings.
These results do not certify whole-system zero allocation or wireless operation.
The subsequent [allocation audit](../firmware/qualification/esp32c3-allocation-audit-20261027.json)
adds boot/application-startup/normal counters and a fixed 1 KiB idle stack.
On its final image, application startup and normal operation observed zero SDK
heap events and kernel allocator entry attempts; all post-startup object counters
remained zero through twenty cycles, rollback and reboot restore at revision 342.
The fixed internal-RAM footprint is 230,190 bytes within the 278,528-byte budget.
Hook coverage, deferred stack measurements and wireless limits remain explicit.
The [integration record](../firmware/qualification/esp32c3-system-integration-20261026.json)
retains passing host checks, both fresh target builds and native UART object/
symbol checks; these do not complete physical or whole-system memory acceptance.
The user supplied `Project/ESP/Tribunal1/requirement_change.txt`, titled
"AAI13 - Change Requirement: Static Memory Allocation" (SHA-256
`73902dfa6846a0458e14b1c291043b1b4a7685ce553a95f8ac9f40b3e7859949`).

The mandatory change prohibits application heap allocation, requires all kernel
objects to be created during startup, and requires fixed per-module RAM budgets
within a declared total. It covers parsing, validation, apply/rollback,
communication, supervision, inter-task communication and temporary buffers.
Oversized inputs and exhausted capacities must fail safely. The document permits
a justified Wi-Fi-driver exemption; it does not grant blanket exemptions to the
SDK, C library, native UART or kernel. Startup permission to create kernel objects
does not authorize application `malloc` during startup.

##### Confirmed changes and dependency boundaries

The table below preserves the original pre-integration impact review. Its paths
describe the earlier implementation, not the current checkpoint above.

| Area | Current evidence | Required direction |
|---|---|---|
| JSON parsing | `src/config/cfgjson.c` calls `malloc`/`free` through cJSON hooks. The 32 KiB / 1024-allocation cap bounds a heap; it does not remove it. | Replace with explicitly sized parser storage, such as fixed node/string pools or a bounded arena with one parse lifetime. Preserve alignment, depth/size/duplicate-key checks, partial-failure cleanup and unchanged active configuration on rejection. Audit library callees as well as hooks. |
| Workers and rollback | `src/runtime/tkport.c` creates/deletes kernel tasks for each plan; `activate.c` reconstructs the previous plan on failure. No `TA_USERBUF` is supplied, so the pinned kernel allocates task stacks through `knl_Imalloc`. | Create a fixed worker pool with fixed, owned stack storage at startup. Rebind/restart existing objects after cooperative quiescence; retain cleanup ownership and rollback guarantees. |
| Protocol/GPIO locks | `c3i2c.c`, `c3spi.c`, `c3uart.c`, `c3pulse.c` and `c3pwm.c` create mutexes on open and delete them on close. Apply and discovery can open these after startup. | Create every supported resource's synchronization objects at startup, including resources absent from the initial profile. Separate permanent object lifetime from temporary hardware ownership. |
| Native UART | `c3uart.c` closes/reopens `sera` to apply attributes. Each `tk_opn_dev` reaches `kernel/tkernel/deviceio.c`, which creates an abort semaphore; close deletes it. | A boot-created adapter mutex alone is insufficient. Design a persistent native-device lifecycle and a supported reconfiguration path that creates no new kernel objects. Preserve the pinned dependency and exclusive UART ownership. |
| Profile storage / SDK | `c3store_open` calls `esp_partition_find_first`. In the pinned SDK, `components/esp_partition/partition.c` allocates its iterator with `malloc` and lazily builds partition records with `calloc`. | Replace the application's allocating lookup path with bounded, checked storage/layout handling. Audit all reachable flash/SDK calls; do not simply remove partition validation. |
| Budgets and evidence | Profile admission bounds some resources, but there is no complete per-module static-RAM allocation and no no-heap / startup-object gate. | Declare and enforce budgets, inventory all boot objects, inspect the real link map and demonstrate exhaustion plus repeated apply/discovery/recovery without new allocations or objects. |

The native serial driver and its registration helper also use `Kmalloc` during
boot (`device/ser/ser.c`, `device/common/drvif/msdrvif.c`). These use the kernel's
internal pool, not the parser's libc heap. Account for them and explicitly resolve
their treatment against the startup/static-storage contract; do not claim that
removing the application's direct `malloc` establishes whole-system compliance.
The kernel supports caller-supplied task stacks through `TA_USERBUF`/`bufptr`.
Any adaptation must respect the owned-wrapper/pinned-dependency policy.

The SDK flash read path has a conditional heap-backed temporary-buffer fallback
for destinations that cannot be read directly. Current internal-RAM buffers can
take the direct path; this review does **not** establish that every flash operation
allocates. Verify the actual reachable paths, lazy locks, numeric conversion and
startup calls before allowing a no-heap claim. Symbol presence alone is not proof
of execution, and a source grep alone is not proof of absence.

##### Reusable design and measured baseline

The controller, transfer pump, profile-storage scratch, candidate/previous plans,
device-context union, sample cache, health/watchdog state and message queues
already use fixed storage. USB uses the hardware FIFO through a bounded polling
adapter. Retain their behavior and assign explicit budgets; their current design
is a starting point, not a complete certification. The installed application uses
USB for configuration, with no project-owned network path to exempt. Required
on-device networking must budget its own messages, queues, service stacks and
work buffers independently of any justified Wi-Fi-driver exemption.

These are **selected linked object sizes**, not proposed budgets or total memory
usage. They were read with the pinned target `nm`/`size` tools from
`build/shared-handoff-20261026/app-build/build/uthome_runtime_port.elf`:

| Object / storage | Bytes |
|---|---:|
| Controller (`g_control`) | 16,464 |
| Device binding/candidate contexts (`g_binding`) | 12,800 |
| USB transport pump (`g_pump`) | 10,464 |
| Profile storage scratch (`g_store`) | 8,208 |
| Current and previous activation plans | 3,760 |
| Message-bus queues (`msgbus.c:g_queues`) | 2,240 |
| Telemetry context (`g_telemetry`) | 1,720 |
| Latest-sample cache (`g_cache`) | 1,440 |
| Kernel internal pool (`mtk_default_heap`) | 32,768 |

ELF SHA-256: `34eb94aefe9d3422a380fa37d490e00982d33f29d0c1092b071634b2e884fe3a`.
Sibling map SHA-256:
`7b6f28d0a3c5674711f1d609cf3b29a3551bea3ed74fcb61008902b1dc43620b`.
These are retained build artifacts, not a fresh build or a new observation of the
connected board. The table excludes other globals, SDK memory, code in RAM,
stack allocations and the parser heap. A final budget must account for all of
them without double-counting stacks carved from a kernel pool or aliased RAM
sections. Moving the parser to static storage moves its reservation into the map;
it does not make that RAM free.

##### Migration and acceptance order

The user has authorized this effort to implement all remaining subsystem work.
The [overall delivery plan](../README.md) is the canonical
cross-system sequence; the steps below give the firmware-specific migration.
Do not wait for the original member assignments to resolve parser, workers or
storage. Preserve their existing HAL contributions and the current source/bench
evidence while completing the remaining paths.

1. Use the wireless feasibility inventory above to define startup completion,
   module budgets, kernel-object inventory and fixed stack capacities, including
   separate infrastructure reservations. The current four-worker / 16 KiB aggregate limit admits
   individual stacks of 1..8 KiB. The subsequently supplied tribunal discussion
   selects four permanent 8 KiB stacks initially (32 KiB), then measurement and
   right-sizing. That 32 KiB reservation is now implemented; the 16 KiB aggregate
   requested-budget admission remains conservative and is not physical stack RAM.
   The complete wireless system budget remains open. Keep firmware admission, host validation and
   advertised capabilities consistent. See the
   [AI adaptation plan](../ai/README.md)
   for Member 4's dependencies and proposed allocation.
2. Replace parser/application allocation and the allocating storage lookup;
   precreate workers, HAL locks and native-device resources. Reconfiguration must
   bind supported devices to reserved capacity, not create tasks dynamically.
3. Preserve failure isolation and add checks for parser/storage exhaustion,
   oversized packages, full queues, worker capacity, discovery cleanup and rollback
   at maximum occupancy. Instrument allocation paths and kernel object creation
   after startup; repeated successful operation is not sufficient proof.
4. Run the mandatory C gate, affected warning-clean tests and a fresh real-target
   build. Retain the linker map, module attribution, stack/call-depth evidence,
   boot-object inventory and clearly scoped exceptions in the repository. Repeat
   hardware acceptance for the new lifecycle; retain old results unchanged.
5. Recollect timing evidence on the compliant build before qualifying AI for that
   build. Keep existing datasets tied to their original firmware identities.

This review treats the RAM restriction as applying to the embedded system;
desktop Python training/Configuration Desk are separate host processes. The
existing six-input C inference template already uses an immutable bounded tree,
with no allocator or recursion. The newer eighteen-input protocol model has
[isolated C3 parity/timing evidence](../ai/qualification/model-target-probe-20261027.json),
but is excluded from ordinary application firmware because its statistical
benefit gate failed. The fixed adviser controller and typed features are
host-tested; deployment still needs an eligible model, integrated transport and
loaded sampling checks. Retain normal validated configuration when AI abstains.
No UI redesign is required by this change.

## 6. Static AI checkpoint table

Source: [ai/README.md](../ai/README.md).

The authorized Member 4 work now has a versioned execution sidecar, a gated
collector, fixed measurement helpers and a separate eighteen-input C exporter.
See [verification](../ai/qualification/static-adaptation-20261026.json). The integration
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
| Fresh scoped studies | Primary: 192 runs / 70,272 observations; corrected stress: 80 runs / 29,280 observations, both with profile restoration. The seven-node and five-node models passed Python/C parity but both failed the unchanged benefit criterion. See [primary](../ai/qualification/protocol-static-study-20261027.json) and [stress](../ai/qualification/protocol-stress-study-20261027.json). Radios and unseen peripheral types remain outside their scope. |
| Model release | No model is eligible for deployment. The primary classifier passed two isolated target parity/timing boots; this does not replace its failed statistical gate or qualify loaded inference. Ordinary firmware and the Desk remain unchanged by these experimental models. |

## 7. Obsolete prerequisite status

Source: [ai/README.md](../ai/README.md).

The prerequisite
work below remains incomplete, but is now included in this implementation effort
rather than waiting on another member.

## 8. Existing adviser versus future integration

Source: [ai/README.md](../ai/README.md).

A future controller wrapper must implement those checks and
normal reviewed Apply.

## 9. Adviser preparation test counts

Source: [ai/README.md](../ai/README.md).

The [preparation record](../ai/qualification/controller-preparation-20261027.json)
binds source snapshots, the 111-test AI regression, five additional feature tests
and standalone C3 object measurements. It does not permit deployment.

## 10. Standalone adviser measurements

Source: [ai/README.md](../ai/README.md).

Nine compiled C99 host tests cover cancellation, stale results, bounded capacity,
invalid predictors, timeout and preservation of an existing busy request. The
C3 object evidence under `build/system-completion-20261026-233938/adviser-object/`
records 914 bytes of code/read-only data, zero writable globals, and a largest
compiler-reported function stack of 32 bytes. This is not a whole-call-chain or
linked-target timing qualification.

## 11. Feature-builder object measurements

Source: [ai/README.md](../ai/README.md).

`adfeat` has no writable global state; its standalone C3 object uses 706 bytes
of code/read-only tables and a compiler-reported 112-byte function stack.

## 12. Remove session-specific UI timing

Source: [ai/README.md](../ai/README.md).

The following Desk proposal is prepared for review after the user wakes; no UI
code or visual styling has changed.

## 13. Tribunal implementation chronology and unexecuted study plans

Source: [ai/README.md](../ai/README.md).

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

## 14. Resolved UART lifecycle prerequisite

Source: [ai/README.md](../ai/README.md).

The native UART's hidden open/close semaphore lifecycle must also be resolved.

## 15. Resolved profile-storage prerequisite

Source: [ai/README.md](../ai/README.md).

SDK storage allocation remains a prerequisite from the firmware audit.

## 16. Completed October protocol studies

Source: [ai/README.md](../ai/README.md).

##### First measured protocol result — 26 October

The [study record](../ai/qualification/protocol-study-20261026.json) retains 96 runs,
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

The [fault analysis](../ai/qualification/protocol-analysis-20261026.json) separates
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

##### Post-fix measured result — 26 October

The subsequent [study](../ai/qualification/protocol-study-shared-20261026.json) collected
**96 entirely fresh runs**, 24,576 observations and 48 labelled candidates on the
corrected firmware. It used the eight original development workloads, with two
new validation and two new test workloads absent from the entire first study.
The [frozen specification](../ai/examples/protocol-study-postfix.json) retains their
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

The [analysis](../ai/qualification/protocol-analysis-shared-20261026.json) retains the
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

## 17. Offline model and recorder pilot results

Source: [ai/README.md](../ai/README.md).

#### Results and remaining work

The [2026-10-13 record](../ai/qualification/offline-20261013.json) contains 48 scenarios,
20 families, 576 runs and 192 candidate examples. The 21-node tree chose the best
tested interval in 8/10 held-out scenarios; the simple rule achieved 9/10. Both
missed the goal once. There is no demonstrated reason to replace the rule.

C matched Python on 1,248 vectors. A standalone C3 compile reported 584 bytes of
code/read-only data and no writable static data; it was not a linked firmware
build or a board measurement. All recommendations remain non-deployable.

The [26 October recorder pilot](../ai/qualification/recorder-pilot-20261026.json)
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
[static-memory proposal](../ai/README.md). All proposed
budgets and the retained 10 ms loaded compute-latency goal still need measurement
and integration into the team's total resource allocation.

## 18. Offline model handoff check

Source: [ai/README.md](../ai/README.md).

All source hashes listed in the offline evidence
still matched on 18 October 2026. No new training or board testing was performed
for that handoff check.

## 19. Old C gate count

Source: [ai/README.md](../ai/README.md).

The current full gate covers 229 owned C/H files with no findings or tool/policy
errors. Historical counts below remain tied to their original artifacts.

## 20. Earlier C gate count

Source: [ai/README.md](../ai/README.md).

The root BARR-C gate checked 194 owned C/H files with zero findings/tool errors.

## 21. Static-core studies, target inference results and stress-study chronology

Source: [ai/README.md](../ai/README.md).

#### Static-core study and measured costs (27 October)

The [USB-core checkpoint](../firmware/qualification/esp32c3-workcost-20261027.json)
provides the execution context for a new 192-run study under
`build/protocol-study-20261027-static-r2/`. Its frozen source tree, specification,
identity and predeclared evaluation are retained there. Sixteen families are
development data; four validation and four test families were reserved before
capture. Earlier revealed holdouts may be used as development coverage, never
as fresh independent evidence. The new evaluation combinations are distinct.

The [completed result](../ai/qualification/protocol-static-study-20261027.json)
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
[two-boot target probe](../ai/qualification/model-target-probe-20261027.json) matches
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

The [completed corrected stress study](../ai/qualification/protocol-stress-study-20261027.json)
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

The [training-only design review](../ai/qualification/primary-training-design-review-20261027.json)
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

## 22. Obsolete unlinked work-cost handoff

Source: [firmware/src/runtime/README.md](../firmware/src/runtime/README.md).

`workcost.c/.h` is an independently tested C99 integration helper. It is not
linked into the current application and does not change worker scheduling.
The runtime owner should reserve one zero-initialized `workcost_stats_t` per
permanent slot (44 bytes each / 176 bytes for four on host and C3), publish only
bound sources, and update/snapshot under the existing shared guard. It creates
no tasks, queues, locks or heap allocations. Reset/rebinding must change the
associated generation before exposing another window.

Supply step wall time, completion lateness, serialized bus waiting and time
inside transfer calls, actual transferred bytes/operations and per-source
errors/timeouts. Timeouts are a subset of errors. Waiting and transfer-call
time must not overlap and their sum cannot exceed step wall time. These are
not measurements of CPU busy time or electrical wire time. Clock acquisition,
transfer instrumentation, guarded updates and telemetry encoding remain the
runtime/driver integration work; unavailable measurements are never zero-filled.

## 23. Standalone workcost sizes

Source: [firmware/src/runtime/README.md](../firmware/src/runtime/README.md).

The C3 object
is 254 bytes text, no writable globals, with compiler-reported 16-byte stack for
`workcost_add` and zero for its helper. This excludes caller state and future
instrumentation/locking overhead.

## 24. Runtime fixture pass counts

Source: [firmware/src/runtime/README.md](../firmware/src/runtime/README.md).

The real worker-port fixture passed 369 checks on each of two resets; its
activation extension passed 514 on each of two resets. These are kernel/typed
plan tests, not qualification of every peripheral or flash/power-loss behavior.
See the [evidence index](../firmware/qualification/README.md),
[board fixture](../tests/target/runtime_port_board/README.md) and
[software suites](../tests/README.md).

## 25. Logical admission versus permanent stack RAM

Source: [firmware/CONTRACTS.md](../firmware/CONTRACTS.md).

four queues, depth eight, 64-byte items, and 16 KiB total peripheral stacks.
Disabled instances reserve wiring and metadata but no active worker stack.

## 26. Stack ledger correction

Source: [firmware/CONTRACTS.md](../firmware/CONTRACTS.md).

| Peripheral stacks | Sum admitted active stack requests, at most 16,384 bytes; dormant prepared workers also consume the actual port budget |

## 27. Fixed parser ledger

Source: [firmware/CONTRACTS.md](../firmware/CONTRACTS.md).

Bounded cJSON allocation, binder candidate and retained activation plans; use the current ELF and allocation limits

## 28. Work-cost wire integration status

Source: [ai/contracts/README.md](../ai/contracts/README.md).

delta window. These fields are not on the current telemetry wire and are not
new model inputs. Runtime integration and independent calibration remain open.

## 29. Test-suite checkpoint lists

Source: [tests/README.md](../tests/README.md).

The [static-host build experiment](../firmware/qualification/esp32c3-static-host-build-20261027.json)
adds effective-setting/isolation checks: **183 target tests**, the unchanged
**283-file** pinned C gate and fresh ordinary/diagnostic builds. Its static-host
map passes the unchanged limits, at 251,813 bytes total; selecting static host
globals alone is not a no-malloc claim. The preceding
[GATT software checkpoint](../firmware/qualification/esp32c3-gatt-final-software-20261027.json)
adds two isolation tests: **182 target tests**, the **283-file** pinned C gate,
fresh isolated GATT and ordinary application builds, unchanged aggregate memory
limits and absence of radio/model entry points in the ordinary image. It does
not claim RF execution or static-compliant BLE. The preceding
[software regression](../firmware/qualification/system-software-regression-20261027.json)
records 180 target/host tests, 119 AI tests, the full 281-file C standard gate,
and fresh application/fixture builds. It includes command-9 version-1/version-2
C/Python parity, scheduler window validation, mixed network queue capacities,
the checked-in three-protocol profile and the Windows HTTP rejection fix.
Policy, foundation, runtime, discovery and existing dashboard checks also passed.
Earlier failures and their fixes are retained in the record; assertions,
warnings-as-errors, formatter and time limits were not weakened. Hardware,
wireless/static compliance and model eligibility remain separately qualified.

The [27 October allocation-audit checkpoint](../firmware/qualification/esp32c3-allocation-audit-20261027.json)
records 128 passing target/host tests, 90 AI tests, the 237-file C standard gate,
fresh application/fixture builds and two 909-check fixture boots. Version-3
telemetry parity includes application-startup counters, legacy captures and
maximum fragmented responses. Stack, wireless and model-release qualification
remain separate.

## 30. Incremental board-fixture checkpoint counts

Source: [tests/target/runtime_port_board/README.md](../tests/target/runtime_port_board/README.md).

The I2C software checkpoint also links `i2cbus`, `c3i2c`, the BMP388 adapter
and the unchanged Bosch library. Five invalid-argument/descriptor checks retain
that code in the link without touching pins. This produces an expected 519-check
acceptance image, but that revised image has not been flashed or board-qualified.
The historical 514-check logs apply only to their recorded source hashes.
Actual sensor communication needs a separate configured hardware test after the
breakout wiring is confirmed; this default fixture never opens a real bus.
The fixture now also checks that `c3i2c_init()` returns 0 on its first call
and again on a repeated call. These two checks test return values only, and
the image is unqualified. The proof that only one mutex is created comes from
the host `owntest` creation-call counter, not from this fixture. The same two
return-value checks for `c3spi_init()` add two more; for SPI that proof comes
from the host `spowntest` counter.

The subsequent SPI/OLED checkpoint adds SPI2, shared pin ownership, simple outputs
and the OLED driver/registry adapter. Eight further argument/descriptor checks
give an expected **527 checks** in this image. It is build-only: no new board
run is claimed. No bus or output is opened. All peripheral hardware testing is
deferred to final acceptance, and the historical 514-check image remains the
last physically qualified version.

The UART checkpoint adds three factory/descriptor/invalid-argument checks,
for an expected **530 checks**. UART is never opened by the default fixture.
The new build applies the existing resource patch in its isolated source;
that startup change and the UART wrapper still need physical acceptance.
The latest actually captured image remains the earlier 514-check activation
image. Host radar/ownership scenarios live in `tests/target/uarttest.c`.

#### GPIO software checkpoint

Thirteen additional no-I/O factory/descriptor/invalid-argument checks bring the
fixture to **543 expected checks**. This revised image has not been flashed or
executed. Native RMT/LEDC and GPIO electrical tests remain deferred; host
contracts and recovery are exercised by `tests/target/gpiotest.c`. See
[the manual review](../firmware/drivers/hc_sr04/INTEGRATION.md).

#### Configuration core software checkpoint

Nine additional invalid-argument checks link the portable configuration core,
bringing the fixture to **552 expected checks**. No physical USB/storage
operation is invoked. This image has not been flashed or executed; host codec,
controller and interrupted-save tests are in `cfg_main.c` and
`test_config_transfer.py`. See
[the review](../firmware/src/config/README.md).

#### Admission/binding software link check

New C99 modules and managed cJSON 1.7.19~2 are linked, with nesting limited to
eight. Ten no-I/O additions give **562 expected checks**, not a board capture.
The large binder is not allocated by these link/invalid-input checks. See
[admission contracts](../firmware/src/config/README.md).
The historical 514-check activation image remains physically qualified.

## 31. Historical activation count

Source: [tests/target/runtime_port_board/README.md](../tests/target/runtime_port_board/README.md).

`alive` counters. The 2026-09-28 activation image passed 514 checks per reset.
A failed check

## 32. Fixture memory mode

Source: [tests/target/runtime_port_board/README.md](../tests/target/runtime_port_board/README.md).

The test uses the upstream startup/32 KiB heap and memory-only fixture callbacks.

## 33. Scheduler fixture result

Source: [tests/target/runtime_port_board/README.md](../tests/target/runtime_port_board/README.md).

time: a 100 ms delay advances elapsed/idle counters with a one-ms tick. The
scheduler-enabled fixture passed 1,699 checks on each of two resets. Its native
stack checks run after quiescence; unavailable live worker margins remain null.
The root README identifies the current installed image and active capture owner.

## 34. Mixed UART fixture lesson

Source: [tests/target/runtime_port_board/README.md](../tests/target/runtime_port_board/README.md).

The ordinary fixture prints `[uart-test] NOT RUN` and must not be reported as
native UART fault acceptance. The initial 27 October fixture selected the older
combined mode and failed the no-external-sender check with the radar attached;
that failed capture is retained. Earlier sections below describe historical
images and their original evidence, not the latest source's acceptance status.

## 35. GPIO migration and obsolete build blockers

Source: [docs/gpio-startup-mutexes.md](gpio-startup-mutexes.md).

UART, I2C and SPI now also create their resources once at startup (see the
[C3 port guide](../firmware/hal/ports/esp32c3/README.md)). Dynamic worker tasks
and the JSON allocator are outside this change and still require the separate
system-wide migration.

#### Verification record

Run `python -m unittest discover -s tests/target -p test_gpio_startup.py -v`.
Three source lifecycle guards pass: mutex creation confined to startup,
open preconditions/no handle deletion, and startup ordering before restore and
request handling. These are structural checks, not executed hardware-adapter
or kernel failure-injection tests. The board fixture also invokes both startup
functions twice to exercise idempotence when the fixture is built/run.

Pinned clang-format 20.1.8 was applied. The full style gate was run; changed
files have zero findings, but the repository remains failing with 588 findings
and six formatting errors in the six older starter files. These were not
exempted or modified. Report: `build/gpio-startup-standard.json`.

Target build was attempted but not completed: the full style gate blocks it,
the RISC-V compiler is absent from PATH, and the selected kernel/SDK sources
are not available in their documented local locations. No target compilation,
new map sizes, physical tests or whole-system compliance is claimed. Existing
qualification records remain untouched. Before merging, resolve the existing
gate failures, restore the pinned toolchain/dependencies, build both application
and fixture modes with `tools/target/qualify_kernel.py`, and verify repeated
open/close/configuration replacement plus mutex-exhaustion startup on target.

## 36. UART storage original pending build status

Source: [docs/uart-static-storage.md](uart-static-storage.md).

Update: the standard-gate cleanup and cloud build setup are documented in
[GitHub Actions setup](github-actions.md). Earlier failed-gate records below
are retained as history; hardware/target evidence still requires execution.

This implementation supersedes the unresolved UART-record `Kmalloc` design
in `uart-startup-allocation.md`. It changes project-owned source and build
configuration only. The pinned native files remain byte-for-byte unchanged.
Target compilation, map inspection and board execution remain pending.

## 37. UART storage failed-gate and unexecuted-test checkpoint

Source: [docs/uart-static-storage.md](uart-static-storage.md).

Fresh local checks: six UART lifecycle source guards plus five compiler-routing
unit tests pass. The pinned formatter/style gate ran; no new-file findings
remain, but 588 pre-existing findings and six policy/format errors in unrelated
files prevent an overall pass. No gate was disabled or exceptions broadened.
The target build attempt stopped because ESP-IDF is unavailable/unactivated.
Consequently no target compile, executable allocator test or linker-map pass
is claimed.

Board acceptance now checks seal idempotence, rejected late requests and no
state loss on NULL frees. These checks are added, not yet executed.

## 38. UART storage acceptance boundary

Source: [docs/uart-static-storage.md](uart-static-storage.md).

configuration. Target ABI/build and runtime evidence remain pending.

## 39. UART retained-handle original review status

Source: [docs/uart-startup-allocation.md](uart-startup-allocation.md).

#### UART retained-handle adaptation â€” 2026-10-20

Update: the two UART-record heap paths now have a project-owned fixed-storage
adaptation; see [UART fixed native storage](uart-static-storage.md). Statements
below describing unresolved native Kmalloc are the earlier review record;
target verification and system-wide compliance remain outstanding.

Status: implemented for review; source lifecycle guards pass. Target build,
register/wire verification and final RAM accounting remain outstanding.
This is not evidence of full AAI13 compliance.

## 40. Provisional UART RAM table and superseded verification records

Source: [docs/uart-startup-allocation.md](uart-startup-allocation.md).

#### UART RAM accounting â€” no exemption

Provisional UART reservation: **2,048 bytes persistent RAM**, plus **128 bytes
of task-stack headroom** for wrapper locals/scratch (part of, not additional to,
the owning task's fixed stack budget). These are proposed limits, not measured
usage. Do not approve them or add them to a claimed compliant system total
until target sizes, allocator overhead and stack usage have been checked.

| Charged item | Quantity / fixed capacity | Verification needed |
| --- | --- | --- |
| Wrapper state and borrowed `uartio_t` | One each, persistent; no application heap | Linker symbols `g_state`, `g_port`, alignment |
| Operations/name constants | One table and one name | Check map placement; charge RAM if copied there |
| Native RX and TX buffers and serial DCB | Two 513-byte arrays, 512 usable bytes each, plus indices/attributes | `sizeof(T_SER_DCB)` and allocator metadata; arrays are already inside the DCB, do not count twice |
| Native low-level state | One unit's caches/metadata | Native map symbols and section placement |
| Native mSDI record | One `T_MSDI` | `sizeof(T_MSDI)` and allocator overhead |
| Native registration lock | One FastLock semaphore | Kernel semaphore control-block pool contribution |
| Native open state | One open control block and abort semaphore | Kernel open/control-block pool contribution |
| Wrapper mutex | One persistent mutex | Kernel mutex pool contribution |
| Synchronous I/O requests | Serialized; use native device-manager request pool | Attribute the reserved request/control pool and manager overhead once |
| Temporary discard scratch | One 64-byte buffer; no recursion | Compiler stack-usage output plus nested kernel call depth |

IMPORTANT: `msdi_def_dev()` unconditionally calls `Kmalloc(sizeof(T_MSDI))`
and `CreateLock`; `CreateLock` creates a semaphore. Serial DCB allocation also
uses `Kmalloc` when `TK_SUPPORT_MEMLIB` is enabled (otherwise static storage).
Startup-only allocation does not mean heap-free allocation. No UART exemption
has been assumed. If AAI13 prohibits these native allocations too, this design
still needs an approved fixed-storage native registration route or an owned
replacement driver/registration layer. The present change intentionally does
not modify the pinned dependency to conceal that gap.

A linker map alone cannot show the payload of a runtime `Kmalloc` allocation.
Combine the map with the pinned configuration, allocation-call audit, target
`sizeof`/pool capacities, allocator metadata and startup high-water evidence.
The total system budget must include other modules and shared kernel pools;
no measured total-system claim is made here.

#### Verification record and remaining acceptance

- Five `test_uart_startup.py` source guards pass. Three GPIO startup guards pass.
  These check allocation placement, state separation, sequence/bounds, preserved
  fault handling and startup ordering; they do not execute native UART code.
- Full pinned formatter/style gate was run. The changed files have no findings;
  the repository gate remains blocked by existing findings in unrelated
  pwm_motor/uart_gpio files. See `build/uart-standard-check.txt`.
- Target build was attempted with `qualify_kernel.py build --kernel
  build/uart-kernel-reference --runtime-port --configuration-app`. It stopped
  because ESP-IDF was not activated/available. See `build/uart-target-build.txt`.
- No target binary, map, register readback or wire test is claimed.

Before accepting this adaptation:

1. Restore a clean required style gate and activate the pinned SDK/toolchain.
   Build using the existing compatibility/resource patches and preserve logs,
   compiler commands, linker map and stack-usage evidence.
2. Instrument kernel object creation and allocation counters. Initialize twice,
   then repeatedly open/close/reconfigure; prove counters do not increase after
   startup and the native descriptor remains unchanged.
3. With a configurable external UART peer/logic analyzer, exercise 9600, 115200,
   256000 and 460800 baud; all supported 5â€“8 data bits, parity none/odd/even,
   and 1/2 stop bits. Verify registers and wire timing after each change without
   native reopen. Self-loopback alone cannot prove the actual baud rate.
4. Queue TX and RX, close and claim with a different owner. Verify old software
   bytes do not appear and new traffic works. Repeat with incoming traffic
   across the close/open boundary, using explicit peer session markers.
5. Inject full RX, framing/parity errors, full TX, mutex contention, short I/O,
   failed attribute writes, cleanup failure and unlock failure. Verify bounded
   return, count semantics, retained ownership, retry or reboot policy.
6. Measure and enforce the UART reservation and total application budget. Resolve
   the native Kmalloc compliance decision explicitly; UART is not Wi-Fi.

Manual C review: paired declarations/callers updated; C99, fixed locals and no
recursion; no new application allocation; polling calls and capped snapshots;
owner required for lifecycle, worker mutex for transfers; native private header
usage is intentionally pinned and requires re-review on dependency changes.
Hardware timing, target ABI compilation and actual memory totals are pending.

#### Follow-up requirement review

A failed logical open now makes one STOP attempt before returning, retaining
ownership. A failed mutex unlock also makes one STOP attempt and poisons the
port, retaining ownership until reboot. Neither path creates kernel objects.
These defensive branches require fault-injection execution on the target;
the source guard alone does not validate hardware behavior.

Six UART source guards now cover these branches. The earlier five-test record
above describes the initial implementation, not this follow-up run.

Requirements still NOT established: hardware baud/framing correctness across
all supported modes; target compilation; measured/enforced complete UART RAM
budget and total system budget; absence of native heap allocation. The proposed
2,048/128-byte reservations are unverified planning values, not compliant budget
evidence. Native `Kmalloc` remains a compliance gap, not an exemption.

## 41. UART acceptance original blocked-status matrix

Source: [docs/uart-acceptance.md](uart-acceptance.md).

#### UART acceptance checklist and fresh evidence

Update: the standard-gate cleanup and cloud build setup are documented in
[GitHub Actions setup](github-actions.md). Earlier failed-gate records below
are retained as history; hardware/target evidence still requires execution.

Status: NOT COMPLETE. No target execution or physical baud/framing pass is
claimed. This record distinguishes implemented tests from executed tests.

#### Required tests

| Requirement | Implemented check | Current evidence |
| --- | --- | --- |
| No creation calls after startup | GDB entry breakpoints on actual `tk_cre_mtx` and `tk_cre_sem` across `uartcheck_run` | Audit-accounting unit tests pass; target audit pending |
| Baud/framing reach hardware | Board cycles through 4 rates x 4 data lengths x 3 parities x 2 stop lengths | API exercise implemented; physical/register verification pending |
| Reject wrong owner | Real adapter read/write/close calls with different owner | Board assertions added; not executed |
| Report overflow without growth | Inject more than the native RX ring capacity; expect OVERFLOW and zero returned count | Board assertions added; ring capacity fixed at compile time |
| No stale data between owners | Seed native RX, queue TX, retry cleanup, claim next owner and check empty RX/TX plus fresh RX | Board assertions added; not executed |
| Cleanup failure prevents reuse | Acceptance-only read hook returns E_IO once during cleanup; assert retained owner, blocked new open/read, successful retry | Board assertions added; not executed |
| Startup precedes restore/requests | Application ordering and no Detect/Apply initialization | Source guards pass |
| Failed startup stays unavailable until reboot | One-shot attempted/ready state; no creation in ordinary open | Source guard only; startup failure-injection execution pending |
| Fixed RAM, kernel objects and capacities documented | `uart-static-storage.md` and compile-time 1,280-byte record reservation | Map, stack and full system totals pending |
| Pinned standard gate and warnings-as-errors C tests | Gate and affected host suite attempted | BLOCKED by 588 existing findings and 6 errors; changed files have no reported findings |
| Fresh ESP32-C3 build | Qualification attempted | BLOCKED: ESP-IDF unavailable/unactivated |

## 42. Explicit isolated UART build flag

Source: [docs/uart-acceptance.md](uart-acceptance.md).

Use `tools/target/qualify_kernel.py build --runtime-port`, the pinned kernel and

## 43. UART acceptance local verification results

Source: [docs/uart-acceptance.md](uart-acceptance.md).

#### Local validation record

18 Python source/build-routing/debugger-accounting tests pass. The debugger
unit tests use a fake GDB object; they validate counter logic, not the board.
The affected C host suite was attempted with its required gate and was blocked
before compilation. The fresh target build attempt also failed before compiling
because ESP-IDF was unavailable. Logs: `build/uart-acceptance-standard.txt`,
`build/uart-acceptance-host.txt`, `build/uart-acceptance-target.txt`.

## 44. Obsolete separate publishing-workspace instruction

Source: [docs/github-actions.md](github-actions.md).

The original extracted source folder remains separate from the publishing Git
checkout. Future commits and pushes should run from the Git checkout, with
`build/`, caches and local dependencies excluded.

## 45. CI setup local checkpoint

Source: [docs/github-actions.md](github-actions.md).

#### Local verification and limits

- Pinned C-standard gate: 199 files, zero findings and zero policy errors.
- Checker suite: 54 tests passed; foundation: 26 passed; runtime: 20 passed.
- Target-host suite: 75 passed; four additional CI-contract tests passed.
- Standalone UART/GPIO and optional-output binaries pass under C99 with
  `-Wall -Wextra -Werror -pedantic`.
- YAML loads correctly; Bash syntax check passes for all 13 run steps.
- Preserved Member 5 suite: 60 of 61 pass locally; one symlink test requires a
  Windows privilege unavailable here. It remains enabled on the Ubuntu runner.
- ESP-IDF was not installed locally. No GitHub target-build result is claimed;
  the first cloud run is still required. No hardware was flashed or tested.

## 46. Compatibility notes heading

Source: [docs/github-actions.md](github-actions.md).

#### Behavior-preserving standard cleanup

## 47. Obsolete optional-output style blockers

Source: [docs/optional-outputs.md](optional-outputs.md).

The current full standard gate reports pre-existing violations in six legacy
starter files (`pwm_motor.c/h`, `uart_gpio_io.h`, `uart_gpio_host.c/h`, and
`test_uart_gpio.c`). None is changed by this addition. The runner saves the full
report to `build/output-tests/c-standard.json`, still executes the functional
tests, and returns a failing exit code if the repository gate fails. A functional
PASS must not be represented as a full repository PASS. No gate is disabled and
no exception is added. New-file diagnostics should also be inspected in that
report whenever changing these modules.

## 48. Original S3 UART/GPIO starter handoff

Source: [docs/uart-gpio.md](uart-gpio.md).

#### Member 3: UART and GPIO starter and handoff

#### What is ready

This is standalone, host-tested C11 code. It does not assume an ESP-IDF or
μT-Kernel API. All interfaces are provisional UART/GPIO contracts, prefixed
`ug_`; no shared headers under `firmware/include/uthome/` were changed.
Use branch `feat/uart-gpio` for this module. Commit and push from your Git
checkout after reviewing the changes.

| Location | Responsibility |
| --- | --- |
| `firmware/hal/include/ugio.h` | UART RX/TX, digital I/O, edge callbacks, timed pulse capture, PWM and status contract |
| `firmware/hal/ports/host_sim/uart_gpio_host.*` | Deterministic virtual time, bounded UART buffers, GPIO/IRQ and PWM fake |
| `firmware/drivers/hlk_ld2450/ld2450.*` | Streaming frame recovery, three target slots, timestamp and diagnostic counters |
| `firmware/drivers/hc_sr04/hc_sr04.*` | Trigger/capture request, distance conversion, cooldown and timeout propagation |
| `firmware/drivers/pwm_motor/pwm_motor.*` | Unidirectional active-high PWM adapter; initialise and stop at zero duty |
| `tests/unit/ugtest_main.c` | Protocol, timeout, GPIO and actuator assertions |

From the repository folder containing `firmware`, run:

```powershell
python tools/test_uart_gpio.py
```

Requires Python 3 and GCC (or set `CC` to a Clang executable). Builds in a
temporary directory with C11 and warnings treated as errors; no firmware
toolchain or downloaded dependency is required.

#### Read the code in this order

1. `ugio.h`: a function table lets the same driver call a host fake today and
   the approved board port later. `ctx` holds the port's private state.
2. `ld2450.c`: feed bytes into a fixed 30-byte buffer; locate the header,
   verify the footer, decode each slot, return one report.
3. `hc_sr04.c`: ask the port to capture Echo around a Trigger pulse, then
   convert the duration to millimetres. Scheduling stays in the runtime.
4. `ugtest_main.c`: working examples of initialisation, input injection,
   reads, output control and fault handling.

#### Integration contract for Members 1 and 5

Use the existing `configs/nodes/first-prototype.json` as the UART and sonar
profile. It already contains the intended LD2450 and HC-SR04 settings; avoid
introducing a second conflicting profile format.

| Existing JSON field | Member 3 binding |
| --- | --- |
| UART `port`, `rx_gpio`, `tx_gpio`, `baud_rate`, `rx_buffer_bytes` | `ug_uart_config` then `io.uart_open` |
| UART `data_bits`, `parity`, `stop_bits` | Reject anything other than 8 / none / 1 in this starter |
| Radar `timeout_ms` | Checked conversion to microseconds, then `ug_ld2450_read` |
| Radar `frame` | Validate fixed header/footer/30-byte format; not a generic configurable parser |
| Sonar `trigger_gpio`, `echo_gpio`, `timeout_us` | `ug_hc_sr04_init` |
| Sonar `interval_ms` | Runtime scheduling; require at least 61 ms |
| `destination` | Runtime publishes only successful samples to the configured queue |

The configuration layer must check pins against the exact board, all pin and
peripheral conflicts (including I²C/SPI), bounded buffer limits, and timeout
conversion overflow before starting tasks. The host's 0–63 pin range is a
test convenience, not an ESP32-S3 valid-pin list.

One task owns each driver instance and its I/O context. No function allocates
memory. UART calls allow partial transfers; callers inspect the returned byte
count. The radar reader consumes one byte at a time, so back-to-back reports
remain queued. A timeout or transport error clears its partial frame; a
malformed frame searches for the next header. A 4096-byte work cap also bounds
noise processing. Counters wrap modulo 2^32. The protocol has no report checksum,
so framing checks cannot detect every corrupted payload.

For each iteration: read -> on `UG_OK`, copy the result to the bounded queue;
otherwise publish a health event containing device ID and status. Never
publish the unchanged output buffer after an error. The driver does not create
tasks, publish messages or sleep between iterations. Convert local sample
structs to the team's shared message types once agreed. `UG_BUSY` from sonar
means the sampling cooldown has not expired; reschedule rather than spin.

Before reconfiguration, stop the owning task, quiesce interrupts, disable PWM,
and release port resources. Lifecycle/close/resource ownership will need an
agreed extension before hot reconfiguration; this starter does not supply it.

#### ESP32-S3 port tasks still to implement

- Confirm the professor-approved kernel port/toolchain and run the baseline
  serial message. Do not substitute FreeRTOS APIs for μT-Kernel assumptions.
- Implement `uart_gpio_io` only under `firmware/hal/ports/esp32s3/`. Map finite
  microsecond deadlines to the approved timer and scheduler primitives.
- Implement UART configuration, buffered RX/TX and overflow reporting. Keep
  radar UART separate from the monitoring/configuration channel.
- Implement input/output setup and short ISR callbacks. Pass edge events to
  the runtime using its approved ISR-safe mechanism.
- Arm Echo capture before Trigger. Budget includes Trigger, waiting for rise
  and waiting for fall; missing/stuck Echo must time out and Trigger stays low.
  Use hardware capture or timestamped edges rather than long polling loops.
- Implement PWM with checked frequency/duty and safe initial output. Unsupported
  operations must return `UG_UNSUPPORTED`, never fake success.
- Add resource release, rollback after partial initialisation, and concurrency
  protection with the runtime owner. The host fake does not test real IRQ
  concurrency, scheduling jitter, pin routing or electrical waveforms.

#### Wiring and bench checklist

Use the existing provisional map: radar TX -> GPIO16 (RX), radar RX <- GPIO17
(TX), sonar Trigger -> GPIO4, Echo -> GPIO5 through a divider. Verify the board
pinout before wiring. Both modules need their specified supply and common GND.
The project's 5 V HC-SR04 Echo must not connect directly to the MCU.

The manufacturer radar protocol describes the report structure and signed
encoding used here: [Hi-Link LD2450 protocol v1.03, section 2.3 (mirror)](https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf).
The sonar conversion and pulse timing follow the [HC-SR04 datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf).

| Bench test | Evidence to save | Acceptance |
| --- | --- | --- |
| Radar stream | Raw UART capture and decoded log | Frames and signed coordinates agree; empty slots absent |
| Radar unplug/noise | Status/counter log | Finite timeout, recovery after reconnect, no stale publish |
| Sonar at 100/500/1000 mm | Reference distance and measured values | Record error; agree tolerance with team |
| Sonar unplug/stuck Echo | Logic analyser + elapsed time | Total operation within timeout plus documented scheduler tolerance |
| Trigger interval | Logic analyser capture | Correct trigger pulse; starts separated by at least 61 ms |
| PWM 0/500/1000 | Scope capture using LED/load first | Duty and frequency correct; zero at startup and stop |
| Concurrent peripherals | Queue depth/jitter/overflow log | No unbounded waits; health events observable |

#### Motor extension and demonstration

`configs/examples/pwm-motor.proposed.json` is a disabled device fragment
for review, not a deployable node profile. Its null pin intentionally prevents
accidental activation. Confirm the motor driver, usable PWM pin and frequency
before Member 5 incorporates it into the schema. Use an external motor driver
and appropriate power/protection; GPIO only supplies its control signal.
This adapter does not implement reversing, braking, ramps or current limiting.

The test demonstrates a simple sensor-value-to-duty decision and explicit stop.
A real demonstration still needs Member 1's rule and watchdog: successful fresh
distance below a chosen threshold -> configured duty; distance above threshold,
sensor fault, stale input, task stop or profile disable -> zero duty. Add
hysteresis to prevent threshold chatter. A failed stop command must generate a
health fault; software alone cannot guarantee physical shutdown.

#### Suggested next deliverables

1. Member 3 + Members 1/5: review these provisional signatures, status mapping,
   sample types and ownership; record agreement before promoting shared APIs.
2. Member 3: implement the board port after baseline approval; run radar and
   sonar individually and save wiring photos, serial captures and measurements.
3. Member 1: create the reader tasks and queue/health publication; implement
   lifecycle and stale-input safe-state policy.
4. Member 5: bind validated JSON to these initialisers and reject conflicts;
   confirm the motor extension schema separately from the four-device prototype.
5. Team: integrate with OLED and rerun concurrent/fault bench tests.

Host verification covers fragmented reports, manufacturer example decoding,
bad footer recovery, consecutive reports, timeout cleanup, RX/TX overflow,
interrupt edge filtering/detach, invalid GPIO use, sonar timeout budget,
cooldown/range checks and PWM initialisation/range/stop. Passing it establishes
portable logic behaviour, not target readiness.

## 49. Automation completed software and size table

Source: [firmware/src/automation/README.md](../firmware/src/automation/README.md).

#### Verified software and memory

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

## 50. Retained UART descriptor correction

Source: [firmware/hal/ports/esp32c3/README.md](../firmware/hal/ports/esp32c3/README.md).

The wrapper queries available bytes/space before I/O and reopens once to apply
the native driver's cached settings.

## 51. HAL acceptance scope

Source: [firmware/hal/ports/esp32c3/README.md](../firmware/hal/ports/esp32c3/README.md).

Compilation is verified; physical I2C/SPI/output tests are deferred to final
hardware acceptance.

## 52. SPI before-after test counts

Source: [firmware/hal/ports/esp32c3/README.md](../firmware/hal/ports/esp32c3/README.md).

The [controlled comparison](../firmware/qualification/esp32c3-spi-polling-20261027.json)
has two trials at each of six clocks: 122 render steps per clock. Before correction,
400 kHz through 1.75 MHz timed out every time; afterward, 500 kHz through 4 MHz
completed without errors. At 400 kHz all frames still exceeded the 20 ms deadline.
Peer sensors stayed fresh and forbidden allocation/object counters remained zero.

## 53. Compiled support versus qualification

Source: [firmware/drivers/ssd1306_spi/INTEGRATION.md](../firmware/drivers/ssd1306_spi/INTEGRATION.md).

Target capabilities remain
empty; no existing profile is silently advertised as deployable.

## 54. Application remaining work

Source: [firmware/app/README.md](../firmware/app/README.md).

Physical acceptance
and AI remain deferred, with AI last.

## 55. Discovery D3 status

Source: [schemas/README.md](../schemas/README.md).

The full host review record
and setting recommendations remain D3 work.

## 56. Retired phase numbering

Source: [tools/README.md](../tools/README.md).

Hardware acceptance is checkpoint 8; AI and model tooling remain checkpoint 9.

## 57. Configurator retired checkpoint

Source: [tools/configurator/README.md](../tools/configurator/README.md).

The D2 discovery command requires a newly built application image. Physical USB,
peripheral and persistence acceptance belongs to checkpoint 8.

## 58. Wiring visualiser old bench state

Source: [visualiser/README.md](../visualiser/README.md).

On 20 October 2026, before the user dismantled the bench to travel home,
an empty configuration was applied/saved on the standalone ESP at revision 8,
CRC 2260115177, with no retained peripheral instances or runtime errors.
Local historical evidence:
`../build/hardware-acceptance-20261020/board-alone-empty-configuration-95d7cdc3.json`.
This is not a statement about the board's current state. The session also
observed intermittent radar faults and a failed configuration cleanup;
rewiring has not been shown to fix either issue. Fresh wiring acceptance
remains pending.

## 59. Stale installed discovery features

Source: [visualiser/README.md](../visualiser/README.md).

identification, not automatic model recognition. The last observed installed
firmware lacked the newer I2C/UART discovery feature bits. Current suggested

## 60. Old heap-parser budget and completed profile checks

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

There is also a parser allocation limit: the attempted A+LED profile passed the
Python resource planner but exceeded the C parser's **32 KiB allocation budget**.
The supplied lab profiles therefore separate the radar/LED test from combined A.
All eight generated profiles passed the actual C admission harness as well as the
Python planner during guide preparation; that is not a hardware result.

## 61. Bench logical stack accounting

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

Combined A uses 10,240 bytes of peripheral task stacks, within the 16 KiB/four-worker
limit. This calculation does not measure remaining RAM.

## 62. Remove session-relative acceptance heading

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

#### 7. Run today's acceptance tests in this order

## 63. Retired hardware checkpoint label

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

##### Recovery/resource checks before signing off all of checkpoint 8

## 64. Retired full-acceptance label

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

Passing the wiring tests is useful progress; full checkpoint 8 sign-off also

## 65. Sign-off terminology

Source: [CONTRIBUTING.md](../CONTRIBUTING.md).

Full checkpoint 8 readiness: PASS / FAIL / INCOMPLETE

## 66. Discovery acceptance stale status

Source: [docs/README.md](README.md).

- [Tests](../tests/README.md); D1-D4 software is implemented, physical discovery
  acceptance remains pending.

## 67. Local artifact checkpoint inventory and submission handoff

Source: [docs/README.md](README.md).

#### Local artifacts and review documents

The following are local files, not part of the published repository. Paths are
relative to the `Code` folder unless stated otherwise. Preserve them when
changing workspaces; a new chat does not need a new clone on this workstation.

| Location | State / purpose |
|---|---|
| `build/static-ai-20261026/` | Static AI contracts/export verification, standalone C3 objects, integration flash receipts, twenty discovery/profile cycles, failed-activation rollback and confirmed original settings; [AI record](../ai/qualification/static-adaptation-20261026.json), [bench record](../firmware/qualification/esp32c3-integration-bench-20261026.json) |
| `%USERPROFILE%/esp/hardware-backup-20261026-static-ai/board-before.bin` | Verified 4 MiB backup before the integration image; hash/location in `build/static-ai-20261026/backup.json` |
| `build/hardware-acceptance-20261026/` | Pre-update state/failure captures, corrected build snapshots, fixture serial logs, verified application flash, discovery and bench observations; `backup-reference-fef8c15d.json` locates the full external flash backup |
| `build/shared-handoff-20261026/` | Shared-guard fix: preserved application/fixture build snapshots, two 895-check boots, flash receipts, unchanged configuration readback, before/after analysis and backup reference; [qualification](../firmware/qualification/esp32c3-shared-handoff-20261026.json) |
| `build/shared-handoff-*-20261026.log` | Runtime/target host checks and isolated target-build logs for the shared-guard change |
| `%USERPROFILE%/esp/hardware-backup-20261026-shared-3423ecff/board-before.bin` | Verified 4 MiB pre-handoff-fix flash backup; digest and original temporary location in `build/shared-handoff-20261026/backup-preserved.json` |
| `build/ai-offline-20261013/` | Simulated runs, dataset, readable trained model and C export; see the [AI guide](../ai/README.md) |
| `build/ai-pilot-20261026-074410/` | Eight real BMP388 timing captures, raw journals, identity/build evidence, exact Apply receipts, four-row dataset and confirmed original-profile restoration; [record and hashes](../ai/qualification/recorder-pilot-20261026.json) |
| `build/ai-recorder-final-tests-20261026.log` | 65 passing AI tests, including 15 recorder tests and the 194-file full C gate |
| `build/protocol-study-20261026-r1/` | Superseded initial label policy: collection stopped at 25 runs after identifying an invalid FIFO-consumption proxy; original profile restored. Raw data and source copies preserved |
| `build/protocol-study-20261026-r2/` | Completed v0.2 study: 72 new plus 24 preserved training runs listed in `reuse.json`; trained model/evaluation/dataset under `trained/`, frozen sources and confirmed restoration; [record](../ai/qualification/protocol-study-20261026.json) |
| `build/protocol-study-20261026-r3/` | Completed post-fix 96-run study in `capture-verified/`; seven-node host model/evaluation/readiness in `trained/`, frozen sources and confirmed restoration. Predeclared coverage criterion failed; [results](../ai/qualification/protocol-study-shared-20261026.json), [analysis](../ai/qualification/protocol-analysis-shared-20261026.json). Preserve the zero-run checksum preflight abort in `capture/` |
| `build/protocol-r3-tests-20261026.log` | 76 passing AI tests and full 194-file C gate before the post-fix study |
| `build/protocol-full-tests-20261026.log` | 76 passing AI tests, including 11 protocol-study tests and the 194-file full C gate |
| `build/protocol-v02-full-tests-20261026.log` | 76 passing tests after correcting the OLED latest-cache/FIFO assumption, with the full C gate |
| `build/hardware-acceptance-20261026/query-094151.json` | Post-study active/saved revision 87, fresh BMP388 and radar fault; [analysis](../ai/qualification/protocol-analysis-20261026.json) |
| `build/publish-20261017-*.log` | Six Python suite logs: 243 tests; the seven dashboard tests made the reported total 250. These are historical checks, not reruns on 18 October |
| `build/ai-c-gate-final.json` | Retained 194-file full-gate result; policy and manual-review requirements remain in the [C standard](../C_CODING_STANDARD.md) |
| `../ESP/Tribunal1/Week8_Tribunal1_Template.docx` | Only Version A was edited to match uT-Home. Front matter and Versions B/C were preserved; this document was not pushed to GitHub |
| `build/tribunal_review_20261017/` | Original Word backup, edit script, scope verification and rendered layout checks |

**Tribunal follow-up:** the front-page Team Information table still contains the
template's unrelated personas because it was outside the requested Version A
scope. The user identified this mismatch; replacement roles were explained in
chat but have not been applied to the Word file. Use the
[actual team responsibilities](../README.md) when that
table is authorized for editing. Leave student details for the team to fill in.
Buddy prompts/positions in Version A are proposals, not invented records of
completed individual sessions. The team must confirm them and reconcile the
Week 6 comparisons with its submitted architecture freeze before submission.

On 18 October, the Word file was verified to match the saved Version A edit.
No edits to it or to its neighboring PDF were made during the handoff check.

## 68. Firmware checkpoint and peripheral-test chronology

Source: [firmware/qualification/README.md](../firmware/qualification/README.md).

#### Build and hardware evidence

These dated JSON records and captures are immutable evidence, not a claim that
every newer source revision has been tested on the board. Build-only results
remain separate from physical observations. Keep new runs in new files.

#### Evidence index

The [overnight handoff](../firmware/qualification/system-overnight-handoff-20261027.json) consolidates
272 usable AI runs / 99,552 observations and 45 minutes / 3,996 paired stability
observations on the retained USB core. Both frozen models fail their benefit gate;
neither is deployed. The original three-device profile is restored and serial is
closed. This is scoped acceptance, not full wireless/static/AI product qualification.

The [final read-only GATT diagnostic](../firmware/qualification/esp32c3-gatt-settle-20261027.json) **passed**.
Its board log reports 2 connections,
2 disconnections and 4 reads.
Earlier [sandbox](../firmware/qualification/esp32c3-gatt-20261027.json),
[active-address](../firmware/qualification/esp32c3-gatt-retry-20261027.json),
[static-host](../firmware/qualification/esp32c3-gatt-static-20261027.json) and
[short-disconnect](../firmware/qualification/esp32c3-gatt-access-20261027.json) results are retained.
Windows device access was necessary for the client to open the known board;
closing a client session did not immediately close the physical radio link.
The final client allows eight seconds between sessions, retaining the unchanged
two-connection/four-read board gate. SDK allocations remain disqualifying.

The [latest static-host build experiment](../firmware/qualification/esp32c3-static-host-build-20261027.json)
binds 183 target tests, the 283-file C gate, actual SDK-setting checks and fresh
ordinary/diagnostic builds. Static host globals add 4,352 SDK data bytes and
4,294 total RAM bytes; the diagnostic passes the unchanged budget at 251,813 bytes.
The final GATT bench record above captures allocation counts and connection behavior separately.
The preceding [GATT software check](../firmware/qualification/esp32c3-gatt-final-software-20261027.json) binds
182 target tests, the 283-file C gate and fresh isolated/ordinary builds, with
unchanged aggregate RAM limits. The ordinary image excludes radio/model entry
points. Actual GATT execution remains a separate record. The preceding
[software regression](../firmware/qualification/system-software-regression-20261027.json) binds
the 180-test target suite, 119-test AI suite, full 281-file C gate and fresh core
builds. The [wireless source audit](../firmware/qualification/esp32c3-wireless-source-audit-20261027.json)
identifies remaining static task/timer and SDK compatibility work. These records
do not by themselves establish board execution or complete wireless operation.

The [scheduler/core checkpoint](../firmware/qualification/esp32c3-scheduler-20261027.json) records two
1,699-check fixture boots, twenty cycles, rollback, six rejection cases, reboot,
host disconnect and a 20-minute stability capture. All 1,776 paired observations
kept both sensors fresh and forbidden allocation/object counters zero. Its fixed
internal RAM is 230,152 bytes. Physical OLED and whole-system acceptance remain
separate. A failed first capture summary was retained and safely rerun; unavailable
live worker-stack margins are null, with actual quiescent margins recorded separately.

Latest collection-image evidence is the
[corrected SPI/core checkpoint](../firmware/qualification/esp32c3-spi-polling-20261027.json): two 1,694-check
fixture boots, twenty profile/discovery cycles, rollback, parser/queue rejection,
reboot and a 30-second host-client disconnect. Its linked internal RAM use is
230,090 bytes; application heap and late-object counters stayed zero. The
six-clock before/after comparison isolates the corrected FIFO polling delay.
The [fixed network core](../firmware/qualification/esp32c3-network-core-20261027.json) records the preceding
startup-owner correction. Neither result enables radios. Physical OLED, USB
unplug, radio coexistence and wider hardware acceptance remain open.
Both subsequent AI studies are complete; consult the root README for the active
bench/capture owner before board access. Workload revisions are not firmware changes.

The [radio comparison](../firmware/qualification/esp32c3-radio-diagnostics-20261027.json) records successful
Wi-Fi scan/stop/restart and PC-observed BLE advertising. It also records SDK heap
allocations; separate radio feasibility does not qualify the integrated static system.

The preceding
[allocation audit](../firmware/qualification/esp32c3-allocation-audit-20261027.json): zero observed SDK heap
events/kernel allocator calls during application startup and normal operation;
zero post-startup kernel-object creations through twenty cycles, rollback and
reset/restore. The idle stack is now a fixed 1 KiB buffer. Two ordinary fixture
boots passed 909 checks each; the final revision is 342. The 230,190-byte fixed
internal-RAM footprint passes the declared budget. See the record for hook scope,
source/build evidence, backing artifacts and deferred stack/wireless/AI work.

The preceding
[static-core checkpoint](../firmware/qualification/esp32c3-static-core-20261027.json): fixed parser pools,
permanent worker stacks, checked storage layout, two 909-check runtime-fixture
boots, twenty discovery/profile/save cycles, rollback and reboot restore at
revision 260. The fixed-map budget passes; whole-system allocation/object/stack,
wireless and AI deployment qualification remain open. Native UART injection was
explicitly excluded because its isolated fixture requires peripherals removed.
The earlier failed combined fixture capture is retained in the checkpoint.
The [isolated UART fixture](../firmware/qualification/esp32c3-static-core-20261027-isolated-uart-build.json)
also builds with its fault hook intact; it was not installed or board-tested.

The preceding
[integration bench regression](../firmware/qualification/esp32c3-integration-bench-20261026.json): twenty
discovery/empty/original cycles, active BUSY and target capacity rejection,
followed by failed BMP activation at the previously observed absent 0x76 address
and successful rollback. Original settings are restored at revision 179.
The OLED was user-confirmed readable/updating. This does not qualify heap-free
execution, permanent worker objects, injected cleanup failure or power-loss
recovery. The [AI preparation record](../ai/qualification/static-adaptation-20261026.json)
keeps model export and remaining release gates separate.

| Area | Recorded scope | Record |
|---|---|---|
| Pinned kernel | Build, then C3 revision 0.4 / 4 MB identification and startup/reset captures | [build](../firmware/qualification/esp32c3-kernel-20260928.json), [board](../firmware/qualification/esp32c3-board-20260928.json) |
| Worker port | 369 checks on each of two resets | [runtime](../firmware/qualification/esp32c3-runtime-20260928.json) |
| BARR refactor | Two 369-check resets and repeated four-slot allocation/release | [regression](../firmware/qualification/esp32c3-barr-20260928.json), [signed audit](../tools/c_standard/AUDIT.md) |
| Typed activation | 514 checks on each of two resets; replacement, failure and recovery | [activation](../firmware/qualification/esp32c3-activation-20260928.json) |
| I2C / BMP388 | Software, compiler and build checks | [I2C](../firmware/qualification/esp32c3-i2c-20260928.json) |
| SPI / OLED | Software, compiler and build checks | [SPI](../firmware/qualification/esp32c3-spi-20260928.json) |
| UART / radar | Software, compiler and build checks | [UART](../firmware/qualification/esp32c3-uart-20260928.json) |
| GPIO / distance / PWM | Software, compiler and build checks | [GPIO](../firmware/qualification/esp32c3-gpio-20260928.json) |
| Configuration framing/storage | Portable core and injected failure checks | [core](../firmware/qualification/esp32c3-config-core-20260928.json) |
| Target admission/binding | Parsing, resource admission and simulated driver lifecycle | [admission](../firmware/qualification/esp32c3-admission-20260928.json) |
| USB and flash adapters | Application/fixture builds and host transport/storage checks | [USB/storage](../firmware/qualification/esp32c3-usbstore-20260928.json) |
| Telemetry / health | Wire parity, sample freshness and monitoring | [telemetry](../firmware/qualification/esp32c3-telemetry-20260928.json) |
| Display renderer | Software rendering and application/fixture builds | [display](../firmware/qualification/esp32c3-display-20260928.json) |
| UI / rules | Local workflow, capabilities and bounded output rules | [UI/rules](../firmware/qualification/esp32c3-ui-rules-20260928.json) |
| I2C discovery | Bounded observer, host resolution and target builds | [discovery](../firmware/qualification/esp32c3-discovery-20261012.json) |
| UART/manual discovery | Radar observer, confirmations and target builds | [D4](../firmware/qualification/esp32c3-discovery-d4-20261012.json) |
| Cleanup ownership / earlier bench | Two 881-check fixture boots, verified application update, discovery, profile changes, rejection/rollback and reset restoration; combined stability failed | [26 October results](../firmware/qualification/esp32c3-cleanup-20261026.json), [application build](../firmware/qualification/esp32c3-cleanup-20261026-app-build.json), [fixture build](../firmware/qualification/esp32c3-cleanup-20261026-fixture-build.json) |
| Shared-guard handoff / latest combined bench | Two 895-check fixture boots; 600 seconds with all 4,380 BMP/radar snapshots fresh, 1,200 BMP publications and no new device/UART errors or missed heartbeats | [results](../firmware/qualification/esp32c3-shared-handoff-20261026.json), [application build](../firmware/qualification/esp32c3-shared-handoff-20261026-app-build.json), [fixture build](../firmware/qualification/esp32c3-shared-handoff-20261026-fixture-build.json) |
| System-integration pull with local work retained | 218 host tests, 221-file C gate and fresh application/fixture builds; UART native objects use fixed-storage callbacks and each ELF reserves 1280 bytes for their storage; neither image flashed | [integration](../firmware/qualification/esp32c3-system-integration-20261026.json), [application build](../firmware/qualification/esp32c3-system-integration-20261026-app-build.json), [fixture build](../firmware/qualification/esp32c3-system-integration-20261026-fixture-build.json) |
| Integration image, later physical regression | Full backup, verified flash/configuration preservation, twenty discovery/profile cycles, invalid-capacity rejection, failed-activation rollback and fresh restored samples; static-memory qualification remains open | [bench](../firmware/qualification/esp32c3-integration-bench-20261026.json) |

Companion `*-build.json`, `*-application-build.json`, `*-app-build.json` and
`*-fixture-build.json` files retain exact tool versions, source/artifact hashes
and compiler profiles. Captures are under `logs/`. Their static type/object sizes
are historical accounting, not available RAM or measured stack headroom.
Older manual-review paths resolve through the [documentation migration map](README.md).

#### Peripheral bench observations

**Earlier shared-guard result, 26 October:** after the user deferred further zero-target checks,
investigation found that immediate shared-guard rejection could discard a BMP
publication when a higher-priority task preempted the holder. The kernel wrapper
now permits three acquisition attempts with at most two one-ms delay requests
before ownership. Recursion still returns BUSY immediately; actual delay depends
on scheduling. The [dated record](../firmware/qualification/esp32c3-shared-handoff-20261026.json) separates
the source finding, software/board regression and measured behavior.

| Combined profile observation | Before: 120 s | After: 600 s |
|---|---:|---:|
| Unpublished BMP attempts between endpoint samples | 50 of 238 | 0 of 1,200 |
| Stale BMP snapshots | 21 of 877 | 0 of 4,380 |
| Maximum observed BMP sample age | 1,975 ms | 500 ms |
| New missed-heartbeat events | 8 | 0 |
| New timeout / UART / device-I/O errors | 0 | 0 |

All 4,380 post-update radar snapshots were fresh; maximum observed age was 59 ms.
The first 120 seconds of the new capture were also clean. Application FIFO
overwrites continued as expected for queues with no consumer; these are distinct
from UART receive overruns. The OLED reads the latest cache, and the user confirmed
it remained readable and updating. Active/saved revision 87 / CRC 3623560383 was
unchanged, and the configuration partition readback matched the pre-update backup.

The full 194-file C gate, 76 target host tests, 18 runtime tests and both real
target builds passed. Two fixture boots each passed 895 checks, including forced
preemption of a shared-guard holder. This establishes one combined-profile result,
not universal timing reliability or the cause of the earlier UART failures.
Radar absence acceptance is **deferred at the user's request**. The uncontrolled
target counts in this capture are not an absence test. Hardware-qualified remains
false; old AI study/source identities are preserved and new training requires the
updated build identity and fresh runs.

The later [post-fix AI workload study](../ai/qualification/protocol-study-shared-20261026.json)
completed 96 fresh runs on this same image. Its post-warmup BMP388/radar
observations stayed fresh, with no new missed-heartbeat, UART or device-I/O
events. Its three 1 MHz SPI groups accumulated 1,923 global device-timeout events;
per-device attribution and transfer durations were not measured. The original
profile was restored active/saved at revision 138 / CRC 3623560383, with fresh
BMP388/radar data in the subsequent query. This expands the observed workloads
without qualifying all configured clocks, absence detection or AI deployment.

##### Earlier peripheral observations

These observations were recorded in the hardware guide for 29 September 2026;
they are partial functional checks, not a completed acceptance report.

| Setup | Observed result | Limit |
|---|---|---|
| BMP388 | 0x77, I2C 100 kHz, 500 ms interval, 100 ms timeout started successfully | Earlier 20 ms initialization timeout failed; bus sharing/accuracy not qualified |
| BMP388 + six-pin OLED | SPI 4 MHz; 120.12 s, 240 new BMP samples, no device timeouts/I/O errors/failed workers in that interval; user confirmed live text | 1 MHz refresh timed out; exact OLED controller and internal CS wiring unconfirmed |
| Saved BMP/OLED restart | Revision 5 and CRC restored after USB power cycle; screen resumed, 21 new BMP samples with no recorded errors in follow-up | One functional restart, not power-loss storage qualification |
| BMP/OLED + LD2450 V1.1 | UART RX20/TX21, measured 5 V module supply; revision 7 restored; user confirmed moving positions | USB-fed power observation is not a general supply/current qualification |
| Combined radar interval | 120.3 s, 57 snapshots, 1350 new radar reports and 241 BMP samples; fresh, no new device/UART/worker failures | Two startup timeouts were already present; their source/timing is unresolved |

Queue-overwrite counts matched the configured drop-oldest queues. These are real
historical events, not evidence of a broken sensor or an all-clear health report.

**Unresolved empty-scene test:** The OLED showed `READING UNAVAILABLE`; the user
also moved/touched boards or cables. Counters reached 113 device timeouts and
13 UART frame errors. A disturbed connection is possible, not established.
Repeat with stationary hardware: valid zero-target reports should stay FRESH
with three ABSENT slots. Only one target was observed; accuracy/multi-target
acceptance remains open. Interrupted USB runs do not count as clean runs.

#### Reproduce and finish acceptance

On 26 October 2026, a full flash backup and the corrected application update
succeeded with the reviewed BMP388/six-pin OLED/LD2450 wiring left intact. The
cleanup fix released the previously retained UART instance. Initial physical
discovery observed BMP388 chip ID 0x50 at 0x77 and 11 valid radar reports in one
second; the board's ELF digest matched the retained build. The user confirmed
readable, updating OLED output.

The subsequent 600-second combined capture **failed stability acceptance**:
47 stale BMP388 observations and 477 radar fault observations among 2,401
snapshots. It added 27 missed heartbeats, 478 timeouts and 11 UART framing events;
7,516 queue overwrite events are separate. Radar communication loss persisted
through UART reopen, ESP reset and a user-confirmed full power cycle. The
empty-view attempt therefore did not establish sustained valid zero-target
behavior. Supply/connection diagnosis remains open. Raw local evidence and hashes
are in the dated record; do not replace it with later recovery results.

The [radar isolation follow-up](../firmware/qualification/esp32c3-radar-isolation-20261026.json) records
user-reported DC readings of 5.075 V at the ESP source and 4.817 V at the radar's
supply rails. These were not simultaneous or measured at the module connector.
A 120-second radar-worker-only run still produced 107 fault and 14 fresh
observations. All modules remained physically connected, so this isolates
software workload, not electrical loading. The combined profile was subsequently
restored and saved as revision 26. Module-side power/contact checks remain open.

The later [fan-off retest](../firmware/qualification/esp32c3-fan-off-20261026.json) followed the user's
reported zero-ohm signal check. All 241 BMP388 snapshots were fresh, while all
241 radar snapshots were fault/timeout; 480 new timeout events were recorded
over 120 seconds. Switching the fan off did not restore communication. Empty
revision 27 was subsequently saved successfully for a power-off GPIO20 socket
contact check. This does not establish whether the underlying cause is the
connection, supply, module or UART adapter.

The [TX contact retest](../firmware/qualification/esp32c3-radar-contact-20261026.json), after moving the
GPIO20 termination from A:b35 to A:a35, likewise had 241 fresh BMP388 and 241
radar fault snapshots in 120 seconds. Combined revision 28 is saved.

The subsequent user-observed Bluetooth check in HLKRadarTool tracked movement
in real time. The supplied screenshot identifies `HLK-LD2450_E00E`, firmware
`2.04.23101915`, and one target at 0.27 m, -5.08 degrees and 0.24 m/s. Its baud
field reads `Select Baud Rate`; that placeholder does not identify the current
UART baud. This establishes observed Bluetooth tracking, not the wired output
or ESP receive path. The user then confirmed selecting 256000 baud in the app,
matching the retained ESP profile, and rebooting the radar. No firmware update
or factory reset was requested.
Hi-Link's [app tutorial](https://h.hlktech.com/download/HLK-LD2450-24G/1/HLK-LD2450%E4%BD%BF%E7%94%A8%E6%95%99%E7%A8%8BV1.1.pdf)
documents selecting the baud and rebooting the module afterwards.

The [post-baud retest](../firmware/qualification/esp32c3-radar-baud-20261026.json) still failed: one fresh
radar snapshot and 240 fault snapshots over 120 seconds, with 477 new timeout
events. All 241 BMP388 snapshots remained fresh. A separate one-second UART
discovery after closing the workers received zero bytes, with no receive errors.
The combined profile was restored and saved as revision 30. The intermittent
valid report confirms some successful reception; it does not identify the
cause of the subsequent silence. Next is a user DC voltage measurement at the
GPIO20 receive strip (Board A b35) relative to common ground. The user subsequently
reported `3.015`; interpreted as volts from the requested DC-volts test, this
shows the observed line was not held at ground. A multimeter reading alone
cannot qualify UART waveforms or decode reports. A spare ESP is available but
not yet identified; independent UART observation remains pending.

The user later reported OLED radar readings returning after disconnecting the
phone app. The [recovery observation](../firmware/qualification/esp32c3-radar-return-20261026.json) captured
120 seconds with all 241 radar snapshots fresh, one target in each, and no new
timeouts, framing errors or UART overruns. The radar sequence advanced by 1,348.
There was one stale BMP388 snapshot and one missed heartbeat; application queue
overwrites increased by 1,684. The combined profile remained revision 30 with
the same CRC. This establishes live reception during the interval, not a repair
or Bluetooth causality. The assistant made no firmware/profile/wiring changes
for this run.

The subsequent [connected-app comparison](../firmware/qualification/esp32c3-radar-bluetooth-20261026.json)
also had 241 fresh radar snapshots over 120 seconds, with no new timeout,
framing-error or UART-overflow events. The user confirmed reconnection and was
asked to keep live tracking open; there is no independent Bluetooth state log.
The ESP did not reset between these captures, and revision 30 / CRC remained
unchanged. Phone reconnection therefore did not reproduce the radar fault in
this test. The connected run had 12 stale BMP388 snapshots and 12 missed
heartbeats; application queue overwrites increased by 1,652. These observations
do not establish the source of either intermittent fault. Next is a stationary
empty-view radar check while reception is working, with the phone app
disconnected and no wiring or firmware changes.

After the protocol study restored combined revision 87, a query initially found
radar faults. The user then reported normal OLED readings. The subsequent
[60-second status check](../firmware/qualification/esp32c3-radar-status-20261026.json) captured 121 fresh
radar and 121 fresh BMP388 snapshots, with no new timeouts, UART errors or missed
heartbeats. Radar sequence advanced by 674; sample ages were 0..88 ms. There
were 82 fresh zero-target and 39 fresh one-target snapshots. Application queue
overwrites increased by 848, separately from UART overruns. Profile revision 87,
CRC 3623560383 and runtime generation 58 remained unchanged. No configuration or
firmware changes were made for this check. Reception had recovered, but the
cause of the intermittent failure remains unresolved. Scene occupancy was not
controlled, so the zero-target observations do not complete absence acceptance.

The following [empty-view attempt](../firmware/qualification/esp32c3-radar-empty-20261026.json) began after
the user confirmed they would clear the view for 90 seconds. The capture allowed
15 seconds to leave, then evaluated the following 60 seconds. All 151 radar and
BMP388 snapshots over the complete 75-second capture stayed fresh; no new
timeouts, UART errors or missed heartbeats occurred. The predefined evaluation
window contained 18 one-target and 103 zero-target snapshots. The target was
reported at roughly 14..24 seconds, with y=2431..3673 mm, followed by 51 seconds
of continuously fresh zero-target reports. Thus the full planned 60-second
absence criterion failed. The user subsequently
[confirmed the view was empty by 15 seconds](../firmware/qualification/esp32c3-radar-empty-clarification-20261026.json).
The 18 one-target snapshots after settling were therefore unexpected against
the reported empty scene. Their source is unproven; the later zero-target
reports do not erase them. Revision 87 and wiring stayed unchanged by the
assistant. The original intermittent UART failure remains unresolved.

The user then described an object-dense scene, suspected their chair, and pointed
the module upward. After confirming readiness for a stationary, ceiling-facing
check, the [separate diagnostic](../firmware/qualification/esp32c3-radar-ceiling-20261026.json) recorded
another 75 seconds, again excluding the first 15 seconds from evaluation. All
151 radar and BMP388 snapshots were fresh. Within the 60-second evaluation
window, 109 of 121 radar snapshots reported one target and 12 reported zero;
radar age was 0..84 ms and sequence advanced by 674. The reported target distance
was approximately 0.36..0.70 m, which does not identify a physical object.
No new device timeouts or UART errors occurred, but one missed heartbeat and
1,060 application queue overwrites occurred across the full capture. Revision
87 / CRC 3623560383 and runtime generation 58 remained unchanged. This failed
the ceiling-facing zero-target criterion; it is not a normal-mounting acceptance
result, a diagnosis of the chair/ceiling, or an AI training run.

The user attributed the earlier nearby target to USB and requested a
[handheld repeat](../firmware/qualification/esp32c3-radar-handheld-20261026.json). The attribution was not
verified, and hand/board motion was not instrumented. Every one of the 151 radar
snapshots over 75 seconds was fresh and reported two targets, including all
121 snapshots after the predefined 15-second settling period. There were no new
device timeouts or UART errors. However, six BMP388 snapshots were stale and
five new missed-heartbeat events occurred; application queue overwrites increased
by 1,039. Revision 87 / CRC 3623560383 and runtime generation 58 were unchanged.
This is a handheld diagnostic, not stationary absence acceptance or AI training
data. It did not establish a no-target condition or identify the detections'
source; combined timing/freshness faults remain open.

**Review of the 50-byte-ring hypothesis:** The untouched dependency header has
the upstream value 50, but the retained application's staged header has 513
after `0001-uthome-c3-resources.patch`. Its hash-verified ELF independently shows
`T_SER_BUFF.data` with upper bound 512 (513 slots; 512 usable). That ELF digest
matches the board's last discovery response. The adapter rejects an oversized
request with `UARTIO_INVALID`; it does not silently clamp the capacity. The
radar worker is configured at 10 ms, not 100 ms. Nominal input is 300 bytes/s
in 30-byte bursts, not a continuous 256000-baud stream. Event 4 counts application
queue overwrites; event 7 counts reported UART receive overruns and remained zero
in the cited runs. Scheduling latency and counter-report contention remain
measurement limitations. The specific small-ring explanation is contradicted;
this does not exclude another UART HAL, interrupt or scheduling defect. Local
audit details are retained in
`build/hardware-acceptance-20261026/uart-buffer-hypothesis-review.json`.

Ten timing-profile changes, target rejection of reserved GPIO2, rollback from
absent BMP388 address 0x76 and three software-reset restorations succeeded for
their configuration-lifecycle scope. They do not negate the sensor faults or
qualify interrupted-save behavior. The original combined profile was restored
and saved as revision 24, CRC 3623560383 at the end of that sequence; enumerate
and query again before assuming the board's current state.

Use [target tooling](../tools/target/README.md) for fresh isolated builds,
source verification, backup and intentional flashing. Earlier runs are under
`%USERPROFILE%/esp`; their records identify exact directories. Normalized repo
logs and original raw captures have separate hashes. Do not replace either.

The initial kernel reset stayed in download mode; a watchdog reset recovered
boot. Later complete startup captures show alternating A/B progress. ESP-IDF
startup timestamps were unusually large and are not timing evidence. Kernel
time advanced 101 ms for a requested 100 ms delay: progress, not clock accuracy.

Still required: radar absence (user-deferred), HC-SR04 pulse/voltage checks, PWM/load
tests, broader combined-load timing, physical disconnect/recovery, flash interruptions,
repeat profile/reboot checks and heap/stack high-water measurements. Follow the
[hardware acceptance guide](../CONTRIBUTING.md) and retain honest PASS / FAIL /
NOT TESTED results. Hardware-qualified remains false until a reviewed sign-off.


## 69. Startup lock qualification chronology

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

This fixes the experimentally observed pre-scheduler `pthread_mutex_unlock`
assertion; successful target execution must still be recorded separately.
Two subsequent fixture boots passed 1,694 checks, and twenty application
profile/discovery cycles kept forbidden allocation/object counters at zero; see
the [core bench record](../firmware/qualification/esp32c3-network-core-20261027.json).

## 70. Repeated fixture result

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

these tests. Two subsequent target boots passed 1,694 total fixture checks; the linked core bench record retains their raw output.

## 71. Superseded queue-only budget

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The experimental map budget is `tools/target/netqueue-budget.json`: 12 KiB for
the adapter's fixed metadata/payload/native-ID storage. The application category
increases by 16 KiB and the SDK/kernel category decreases by the same amount;
the total reservation stays 278,528 bytes. This is a USB-core queue experiment,
not a radio-services memory allocation or an increase to the total system limit.

## 72. Superseded tick-rate mismatch claim

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The
SDK tick rate is explicitly 1,000 Hz, matching the native millisecond timer and
avoiding the other vendor shim functions' 100 Hz conversion mismatch.

## 73. BLE RAM budget experiments

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The first linked BLE diagnostic used 269,819 bytes overall but exceeded the
57,344-byte RAM-code reservation with 79,022 bytes. Its immutable `bleprobe-a5`
report records that failure. The next variant selects the pinned SDK's flash-only
controller to reduce RAM code; it removes the incompatible legacy ROM dispatch
patch from compilation and link inputs without changing vendor sources. The
qualifier rejects any remaining patch symbol. Flash operations while BLE is
active remain unqualified: the SDK explicitly documents performance/cache
limitations for this option. This probe performs no such flash writes.

## 74. Wi-Fi RAM budget experiments

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The scan profile disables the SDK's Wi-Fi IRAM/RX-IRAM speed optimizations,
trading peak throughput for internal RAM. The earlier unchanged-pool diagnostic
used 300,522 bytes; this setting reduced it to 280,768 bytes. Both remain above
the 278,528-byte reservation and are not accepted radio budgets. The reports
under `build/system-completion-20261026-233938/wifi-memory-*.json` retain actual
map totals and failures rather than increasing a limit to pass them.

## 75. Superseded startup pool qualification status

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

separate from the unchanged four 8 KiB worker stacks. The qualified preceding
USB core used an 8,448-byte controller stack from its 32 KiB native pool; the
smaller experiment must repeat boot, stack-margin and allocation verification.

## 76. Isolated radio run results

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

#### Current isolated radio observations

The `bleprobe-boot-a1` diagnostic completed advertising, stop and restart with
result zero; a passive PC observer received the `UTHOME-DIAG` name. This establishes
advertising only, not GATT, authentication, coexistence or static compliance.
Application startup made 77 successful SDK allocations; restart made one 16-byte
allocation. Internal free heap fell from 129,096 to 67,436 bytes after startup.
The native task/sem counts rose by two each during startup, with no later creates
in this short test. No broad exemption is inferred for BLE.

The [controlled Wi-Fi comparison](../firmware/qualification/esp32c3-radio-diagnostics-20261027.json)
identified the rejected request as 200 items of eight bytes: it exceeded each
old 1,024-byte partition. The revised layout retains the same 8,192-byte payload
reservation as two 512-byte, five 1,024-byte and one 2,048-byte partitions.
The old image rejected that request; the revised image completed initialization,
two scans and stop/restart with result zero, finding 14 and 18 access points.
All 25 observed queue requests succeeded. No credentials, association, IP traffic
or configuration writes were involved; the configuration partition stayed identical.
This is scan feasibility, not static-compliant Wi-Fi/MQTT/HTTP integration.
The first scan/startup recorded 106 application-startup heap allocations. The
stop/restart/second scan/final stop recorded another 66 allocations totalling
24,552 requested bytes; this is cumulative allocation volume, not peak live RAM.
Two native tasks and two semaphores were created during startup, with no later
native creations in this short run. These observations identify work to remove
or narrowly justify; they do not grant a driver or application exception.
Earlier zero-byte captures remain inconclusive.

## 77. Radio probe linked RAM result

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The revised probe's RAM map is 261,698 bytes and passes the unchanged aggregate
limits, including 57,138 bytes of RAM code against 57,344. That narrow code
margin and the [remaining task/timer/notification audit](../firmware/qualification/esp32c3-wireless-source-audit-20261027.json)
must be addressed before service/coexistence qualification.

## 78. Final GATT checkpoint and restoration status

Original guide: [firmware/src/network/README.md](../firmware/src/network/README.md). Historical excerpt follows.

The [final static-host GATT bench](../firmware/qualification/esp32c3-gatt-settle-20261027.json)
**passed** with 2 board-observed connections,
2 disconnections and 4 reads.
The PC also checks changing uptime and rejection of writes. Windows needs device
access; two client contexts alone did not establish a physical reconnect. The
eight-second settling interval changes the test client, not the board gate.
This image records 68 application-startup
heap allocations and 56 subsequent allocations totaling
920 requested bytes at the last available checkpoint.
These are cumulative allocation counts/volume, not peak live memory. No late
native creates were observed in this bounded run. Unvisited zero-initialized
observation slots are marked unavailable, not treated as zero heap use.
The original USB core/profile was verified after restoration; no radio diagnostic
remains installed. Full static compliance, sensor/configuration services,
authentication, coexistence and phone acceptance remain open.

## 79. Superseded pending health integration

Original guide: [firmware/src/health/README.md](../firmware/src/health/README.md). Historical excerpt follows.

The caller explicitly registers watches, submits heartbeats and checks queues.
Runtime restart/configuration does not yet remove or replace watchdog entries.
Do not treat an expired or stale watch as an automatic instruction to release a
driver resource. A heartbeat should represent completed work, not timer activity.

Target integration must add synchronization, watch ownership/removal, queue and
runtime event mapping, stack measurements, supervisor scheduling and bounded
report delivery. The host module uses static state and requires serialized access.
See [the runtime integration record](../firmware/src/runtime/README.md).

## 80. Superseded pending messaging integration

Original guide: [firmware/src/messaging/README.md](../firmware/src/messaging/README.md). Historical excerpt follows.

No kernel waiting, timeout scheduling or concurrent access is implemented.

The host integration tests publish from independently bound runtime instances.
The caller explicitly creates queues and provides their IDs to driver step
contexts. Runtime apply/rollback does not replace queues or restore their contents.

Target configuration must map validated queue names and sample types to actual
queue objects, enforce ownership, synchronize concurrent access, and include
creation/cleanup in checked activation. Wire overflow checks to health explicitly.
A display or network transport is an optional consumer of declared sample types;
core messaging is independent of the prototype's device combination.

## 81. Superseded synchronization status

Original guide: [firmware/include/uthome/README.md](../firmware/include/uthome/README.md). Historical excerpt follows.

sensor models. Calls are serialized; the target synchronization layer is pending.

## 82. Obsolete unpublished-branch status

Original guide: [firmware/include/uthome/README.md](../firmware/include/uthome/README.md). Historical excerpt follows.

The component interface and its unit test are reused from Member 1's branch;
shared-header changes in this working tree are ready for team review and have
not been merged or published.

## 83. Unimplemented S3 port and obsolete prototype wiring

Original guide: [firmware/hal/ports/esp32s3/README.md](../firmware/hal/ports/esp32s3/README.md). Historical excerpt follows.

#### ESP32-S3 HAL port

This port maps the public HAL to the selected ESP32-S3 toolchain and board. It
owns pin configuration, peripheral handles, interrupt glue, monotonic timing,
non-volatile configuration storage, and USB serial transport.

The first-prototype resource map is loaded from configuration:

- I²C0: GPIO8 SDA, GPIO9 SCL;
- SPI2: GPIO11 MOSI, GPIO12 SCLK;
- UART1: GPIO16 RX, GPIO17 TX;
- OLED control: GPIO10 CS, GPIO13 D/C, GPIO14 reset;
- HC-SR04: GPIO4 Trigger, GPIO5 Echo.

No driver or service may include ESP32-S3-specific headers directly.

## 84. Superseded discovery-build installation status

Original guide: [tools/config_transfer/README.md](../tools/config_transfer/README.md). Historical excerpt follows.

fixture. An earlier application was used for the BMP/OLED/radar bench checks;
installation and physical acceptance of the newer discovery build are not
established by those observations. Verify the current image/capabilities before
use. See [build, protocol and storage contracts](../firmware/src/config/README.md).

## 85. Historical COM port reference

Original guide: [tools/config_transfer/README.md](../tools/config_transfer/README.md). Historical excerpt follows.

Discover the current COM port; COM8 was used in earlier sessions.

## 86. Clarify compiled versus qualified capability

Original guide: [tools/config_transfer/README.md](../tools/config_transfer/README.md). Historical excerpt follows.

OLED and rule/PWM bindings are now compiled; physical hardware remains
unqualified.

## 87. Correct capability versus hardware qualification

Original guide: [firmware/hal/ports/esp32c3/README.md](../firmware/hal/ports/esp32c3/README.md). Historical excerpt follows.

No GPIO/actuator capability is advertised until the relevant acceptance passes.

## 88. AI study totals in current introduction

Original guide: [ai/README.md](../ai/README.md). Historical excerpt follows.

The original six-input model is an **experimental tree trained on simulated data**.
The eighteen-input protocol model has historical and current real-data records
below. The latest two USB static-core studies total **272 usable runs and 99,552
observations**. Both model contracts
predict whether a candidate interval meets the recorded goal
and health criteria; neither is enabled in ordinary firmware or the UI Apply flow.
The primary model has a separately selected target parity/timing diagnostic.

## 89. Past takeover checkpoint narrative

Original guide: [ai/README.md](../ai/README.md). Historical excerpt follows.

The user has since authorized taking over all remaining system work. Follow the
[overall delivery plan](../README.md) across runtime,
configuration, drivers, measurements, AI and Desk integration. Outstanding work belongs to the integrated completion effort; the working
parser, permanent workers and USB measurements are dependencies to preserve. The historical roles and measured results
remain unchanged. This guide owns the detailed AI contracts and evaluation gates.

## 90. Superseded proposed worker stack wording

Original guide: [ai/README.md](../ai/README.md). Historical excerpt follows.

hard-coding the proposed
   32 KiB stacks.

## 91. Distinguish completed core qualification from future combined gate

Original guide: [ai/README.md](../ai/README.md). Historical excerpt follows.

**Qualify discovery and measurement on the new runtime.**

## 92. Dated allocation-audit map table

Original guide: [tools/target/README.md](../tools/target/README.md). Historical excerpt follows.

For the 27 October allocation-audit application, the fixed-map results are:

| Category | Linked bytes | Declared limit |
|---|---:|---:|
| Application writable state, including cJSON hooks | 123,089 | 131,072 |
| Kernel/SDK writable state and alignment | 68,315 | 98,304 |
| Code occupying shared internal RAM | 38,786 | 49,152 |
| Total internal RAM | 230,190 | 278,528 |
| RTC memory | 60 | 128 |

The map exposes 321,296 usable internal-RAM bytes and leaves 91,106 unassigned.
That is **not** measured free heap or stack headroom. These budgets cover the
current USB core; wireless/AI reservations and stack evidence remain required
before a combined-system release. Allocation/object observations are retained in
the [audit checkpoint](../firmware/qualification/esp32c3-allocation-audit-20261027.json).
The per-module limits
and actual object sizes are retained in the generated record.

## 93. Historical native heap override correction

Original guide: [tools/target/README.md](../tools/target/README.md). Historical excerpt follows.

only to the staged copy and hashed. The retained 26 October application ELF uses
the upstream 32 KiB `mtk_default_heap`; this guide's earlier 64 KiB application
override claim was incorrect. Check each image's map rather than assuming a pool
size. This is not a free-RAM measurement. The

## 94. Historical UART suite pass counts

Original guide: [firmware/drivers/hlk_ld2450/INTEGRATION.md](../firmware/drivers/hlk_ld2450/INTEGRATION.md). Historical excerpt follows.

The mandatory BARR-C gate covers 113 owned C/H files. All 115 Python suite tests
pass (54 checker, 26 foundation, 18 runtime, 17 target), including the extended C
fixture and resource-patch failure evidence. New ordinary code compiles under
C99 on the actual target with enabled warnings treated as errors. Board fixture
additions inspect factories/descriptor sizes only and do not open UART.
See [fresh software/build evidence](../firmware/qualification/esp32c3-uart-20260928.json).

## 95. Historical C gate file count

Original guide: [firmware/drivers/hc_sr04/INTEGRATION.md](../firmware/drivers/hc_sr04/INTEGRATION.md). Historical excerpt follows.

The mandatory unchanged gate covers 132 owned C/H files. Required host suites
include

## 96. Historical GPIO fixture counts

Original guide: [firmware/drivers/hc_sr04/INTEGRATION.md](../firmware/drivers/hc_sr04/INTEGRATION.md). Historical excerpt follows.

The board fixture only inspects factories, descriptors and invalid arguments:
13 additions yield **543 expected checks**, not an executed board result.
These fixture additions were not a physical HC-SR04/PWM test.

## 97. Documentation maintenance convention

Original guide: [README.md](../README.md). Historical excerpt follows.

current contracts; put measured results in dated evidence records.

## 98. Superseded bench revision and progress summary

Original guide: [CONTRIBUTING.md](../CONTRIBUTING.md). Historical excerpt follows.

The latest application has passed software/build checks. Record physical results for each session. Earlier partial results are in the
[evidence index](../firmware/qualification/README.md);
this procedure does not certify the hardware.

The latest [integration bench regression](../firmware/qualification/esp32c3-integration-bench-20261026.json)
passed twenty discovery/profile cycles and a failed-activation rollback check on
the assembled BMP388/OLED/LD2450 setup. The original settings are active/saved at
revision 179; user-confirmed OLED output is readable and updating. Full hardware
acceptance, memory/object instrumentation, cleanup-failure and power-loss tests
remain open. The user-deferred radar absence check remains deferred.

## 99. Undated OLED session wording

Original guide: [CONTRIBUTING.md](../CONTRIBUTING.md). Historical excerpt follows.

128x64 output with the existing SSD1306 driver during this session.

## 100. Superseded wholly pending discovery acceptance

Original guide: [firmware/src/discovery/README.md](../firmware/src/discovery/README.md). Historical excerpt follows.

remain unchanged. Hardware acceptance of this new path is still pending.

## 101. Clarify target-pending manifest purpose

Original guide: [catalogue/README.md](../catalogue/README.md). Historical excerpt follows.

admission tests; the pending target manifest has no adapters.

## 102. Retired checkpoint label

Original guide: [firmware/src/automation/README.md](../firmware/src/automation/README.md). Historical excerpt follows.

Checkpoint 7 implements bounded sensor-to-output rules for the broader platform.

## 103. Remove session-relative output statement

Original guide: [firmware/drivers/hc_sr04/INTEGRATION.md](../firmware/drivers/hc_sr04/INTEGRATION.md). Historical excerpt follows.

power. No wiring has been assumed and no physical output was enabled this pass.

## 104. Earlier documentation consolidation narrative

Original guide: [docs/README.md](../docs/README.md). Historical excerpt follows.

Repeated checkpoint diaries were consolidated on 2026-10-17. Current status and
remaining work are in [the project README](../README.md). Earlier text remains
in Git at commit `37319fbd55ca622779eca498519128e6a33bcaa5`:

## 105. Dependency qualification checkpoint

Original guide: [firmware/third_party/README.md](../firmware/third_party/README.md). Historical excerpt follows.

This is the project-selected independent ESP32-C3 port of TRON Forum's
μT-Kernel. Selection does not imply a separate lecturer endorsement. On
2026-09-28 the task example compiled with the tracked SDK compatibility patch
and passed startup and repeated USB-reset checks on our ESP32-C3 SuperMini.
Application runtime and peripheral integration remain separate work. See
`../SETUP_ESP32C3.md` (now [firmware guide](../firmware/README.md)) and [qualification evidence](../firmware/qualification/README.md).

## 106. Superseded future cJSON pin and prototype MQTT scope

Original guide: [firmware/third_party/README.md](../firmware/third_party/README.md). Historical excerpt follows.

component. Pin the exact version in the future configuration component's
`idf_component.yml` and preserve its resolved dependency lock with the build
evidence. Enforce project size, nesting and allocation limits around parsing.
Do not add a second JSON library. Additional components need provenance.
MQTT is outside the first prototype.

## 107. Obsolete application heap override and baseline path

Original guide: [firmware/third_party/patches/README.md](../firmware/third_party/patches/README.md). Historical excerpt follows.

The application resource patch does not supply the UART HAL wrapper, finite
timeouts, the application's 64 KiB kernel-heap override, runtime worker adapter,
or a complete application build. See `../../../PROJECT_BASELINE.md` (now [platform baseline](../README.md)).

## 108. Retired resource-patch checkpoint

Original guide: [firmware/third_party/patches/README.md](../firmware/third_party/patches/README.md). Historical excerpt follows.

Checkpoint 4b applies the unchanged application patch using
`qualify_kernel.py build --resource-patch` alongside `--compat-patch` in the
isolated UART software build. This does not alter the dependency checkout or
claim that the resource-patched image has run on the board.

## 109. Superseded network deferral scope

Original guide: [firmware/hal/README.md](../firmware/hal/README.md). Historical excerpt follows.

Network APIs are deferred because the first prototype uses USB serial for
configuration and monitoring.

## 110. Superseded missing host-fake status

Original guide: [firmware/hal/ports/host_sim/README.md](../firmware/hal/ports/host_sim/README.md). Historical excerpt follows.

UART, GPIO timing and storage fakes remain future integration work.

## 111. Actuator placeholder scope

Original guide: [firmware/src/services/actuator/README.md](../firmware/src/services/actuator/README.md). Historical excerpt follows.

#### Actuator services

The first prototype has no physical actuator. This directory is reserved for
later command validation, safe-state enforcement, and actuator feedback. It is
excluded from the first-prototype firmware path.

## 112. Historical scenario versus current deployment

Original guide: [configs/scenarios/README.md](../configs/scenarios/README.md). Historical excerpt follows.

`first-prototype.json` covers the initial multi-protocol demonstration. Future
scenarios may include room cooling, bathroom ventilation, server-cabinet
protection, and scheduled energy saving after the configuration-driven runtime
is stable.
