# μT-Home

A configurable embedded platform for INF2004, group AAI13. One firmware image
runs on the **ESP32-C3 SuperMini**; a validated profile selects supported devices,
wiring, timing and sensor-to-output rules. The prototype equipment demonstrates
the platform rather than defining its limits.

## Start here

| Task | Guide |
|---|---|
| Set up the board/toolchain or build firmware | [Firmware](firmware/README.md) |
| Wire and test equipment | [Hardware acceptance](CONTRIBUTING.md) |
| Follow the two-breadboard rebuild visually | [Local wiring visualiser](visualiser/README.md) |
| Open the local configuration application | [Configuration Desk](tools/configurator/README.md) |
| Understand device discovery | [Discovery design](docs/README.md) |
| Prepare experiments and train the timing model | [AI](ai/README.md) |
| Choose or validate profiles | [Configurations](configs/README.md) |
| Run software checks | [Tests](tests/README.md) |
| Change C code | [Mandatory BARR-C policy](C_CODING_STANDARD.md) and [agent rules](AGENTS.md) |

## Resume next session

The [overnight handoff](firmware/qualification/system-overnight-handoff-20261027.json)
records a working **USB static core**, completed AI collection and separate
on-device radio diagnostics. All eight phases have progressed; the complete
wireless, no-malloc, AI-assisted product is **not release-qualified**.
Use the [overall delivery plan](#overall-delivery-plan) for the remaining gates.

**At the overnight handoff, serial was closed and the original three-peripheral
profile was restored.** Close any current Desk/serial client before bench work.
The retained application ELF is
`597ae600b7377256f7dcf23978e4c930cd2c9d1036ee5b8bf0471279134ee801`
(`app-cpu-a1-archive`), active/saved revision **737**, CRC
**3623560383** at the final check. Enumerate and verify again before use.
The [scheduler/core acceptance](firmware/qualification/esp32c3-scheduler-20261027.json)
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
[primary study](ai/qualification/protocol-static-study-20261027.json) has 192 runs;
the [corrected stress study](ai/qualification/protocol-stress-study-20261027.json)
has 80. Both frozen models fail the unchanged benefit gate and remain experimental.
The primary model correctly abstains on three unsafe held-out choices; the stress
model matches the simple rule. Neither is enabled for normal configuration.
The [isolated target probe](ai/qualification/model-target-probe-20261027.json)
passed 1,165 parity vectors on each of two boots; its largest observed four-
candidate batch was 83 microseconds, with no counter changes. This does not prove
loaded inference or new-device generalization. See the
[training-only design review](ai/qualification/primary-training-design-review-20261027.json)
before planning a new varied-goal/protocol-cost study with an analytical baseline.

The study exposed an SPI polling delay. The
[controlled correction](firmware/qualification/esp32c3-spi-polling-20261027.json)
passes 500 kHz through 4 MHz; 400 kHz still exceeds the unchanged 20 ms frame
deadline. Negative data and the unexecuted/rejected earlier study plans are
preserved. The two studies use different frozen firmware contexts and must not
be silently merged or presented as wireless-load or unseen-peripheral evidence.

**Wi-Fi scan/stop/restart passed on the ESP under uT-Kernel.** The
[before/after comparison](firmware/qualification/esp32c3-radio-diagnostics-20261027.json)
isolated the rejected 200-by-8-byte queue; repartitioning the same 8,192-byte pool
allowed all 25 observed requests and scans finding 14/18 access points. BLE
advertising was observed by the PC, and the separate
[read-only GATT static-host diagnostic](firmware/qualification/esp32c3-gatt-settle-20261027.json)
**passed**. These diagnostics retain SDK heap allocations and do not qualify
static-compliant networking, authenticated services, MQTT/HTTP or coexistence.
The [source audit](firmware/qualification/esp32c3-wireless-source-audit-20261027.json)
identifies the remaining task, notification, timer and SDK allocation work.
No credentials or external network association were used. Configuration flash
was preserved and the normal USB image restored after radio tests.
The [first GATT attempt](firmware/qualification/esp32c3-gatt-20261027.json) remains
recorded separately: Windows saw the advertisement but could not open the device.
An [active-scan/public-address retry](firmware/qualification/esp32c3-gatt-retry-20261027.json)
and the [initial static-host run](firmware/qualification/esp32c3-gatt-static-20261027.json)
retain their separate outcomes; no failed record is rewritten. The last test uses
Windows device access from the start of a fresh diagnostic connection window.
The [short-disconnect test](firmware/qualification/esp32c3-gatt-access-20261027.json)
read four changing values and rejected writes, but Windows reused one physical
link across two client sessions. The final test allows eight seconds between
sessions and still requires the board's two-connection gate; the gate is unchanged.

The latest [software/build checks](firmware/qualification/esp32c3-static-host-build-20261027.json)
pass 183 target tests, the 283-file pinned C gate and fresh isolated/ordinary
builds. The [broader regression](firmware/qualification/system-software-regression-20261027.json)
also records 119 AI tests and policy/foundation/runtime/discovery/dashboard checks.
The latest ordinary source build (`app-static-host-isolated-a1-archive`) excludes radio
and model entry points; it is build evidence, while the installed image above
retains its own completed bench acceptance. Pinned SDK/kernel checkouts are pristine.
The Windows parser-test crash and HTTP rejection connection-reset issue are fixed.
No commits or pushes were made during the overnight run. The shared branch
contains source and qualification summaries; the raw captures, build archives
and board backup listed below remain local and are not included in Git.

The fixed adviser controller and typed feature builder are host-tested, and a
concrete [Desk proposal](ai/README.md#static-memory-implementation-checkpoint)
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

### Earlier checkpoints

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
The [integration check](firmware/qualification/esp32c3-system-integration-20261026.json)
passed 218 target/runtime/discovery/AI tests, the 221-file C standard gate and
fresh application/fixture builds. Both source snapshots match the combined
working tree. These results are software/build evidence only.

The subsequent [Member 4 implementation](ai/qualification/static-adaptation-20261026.json)
adds static execution contracts, capture gates, fixed measurement helpers and
the eighteen-input protocol C exporter. The integration application is now
installed after a verified 4 MiB backup. Its
[discovery regression](firmware/qualification/esp32c3-integration-bench-20261026.json)
passed twenty cycles, forty successful empty/original Apply operations, twenty
target capacity rejections, and a separate failed-activation/rollback test.
The original profile is active and saved at revision **179**, CRC **3623560383**;
BMP388/radar readings are fresh and the user confirmed the OLED updating.
Serial is closed. Preserve `build/static-ai-20261026/` and its external backup.

- The user is **Member 4**, but has now authorized taking over **all remaining
  system work**, including runtime, configuration, drivers, observability and
  integration. Follow the [overall delivery plan](#overall-delivery-plan);
  former member boundaries are provenance, not reasons to wait for another
  teammate. Keep the platform configurable through supported drivers/profiles.
  Preserve Member 5's UI and present proposed UI changes before implementing them.
- **Mandatory new priority:** the supplied static-memory requirement prohibits
  application heap use and kernel-object creation after startup. The
  [impact review](firmware/README.md#mandatory-static-memory-change) found parser,
  worker/rollback, HAL-lock, native-UART and SDK-storage changes are required.
  The fetched branch implements the HAL/native-UART portions in source; fixed
  workers and fixed parsing/storage are now implemented. Complete the remaining
  allocation/object/stack and combined wireless verification before qualifying another model for
  deployment. Older bench results retain their pre-integration image identities.
- The group's tribunal discussion specifies four permanent workers, initially
  8 KiB each, fixed parser storage and startup-created protocol locks. The
  [AI adaptation plan](ai/README.md#ai-plan-for-the-agreed-static-memory-design)
  maps those decisions to discovery, resource contracts, fresh measurements and
  bounded controller-side inference. Contracts/export preparation are implemented;
  measurement runtime/telemetry integration now has USB-core evidence; complete
  wireless/static qualification remains pending. The teammate HAL changes retain our cleanup-owner fix.
- BMP388 (I2C), six-pin OLED (SPI) and LD2450 (UART) are assembled and satisfy the
  protocol minimum. HC-SR04, resistors, LEDs and a multimeter are also available.
  Keep jumper wiring intact where practical. Enumerate and identify the ESP
  afresh; neither the last COM port nor the installed image is an assumption.
- The [cleanup correction](firmware/qualification/esp32c3-cleanup-20261026.json)
  restored profile replacement and physical I2C/UART discovery. A subsequent
  [shared-guard correction](firmware/qualification/esp32c3-shared-handoff-20261026.json)
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
  [bench evidence](firmware/qualification/README.md#peripheral-bench-observations)
  for the retained observations, photos-based wiring interpretation and limits.
  Hardware qualification remains false.
- The [live recorder and protocol study](ai/README.md#protocol-workload-study)
  are implemented. The [first measured study](ai/qualification/protocol-study-20261026.json)
  contains 96 runs, 24,576 observations and a nine-node host-only tree. It abstained
  on both test families and made one failing validation recommendation; useful
  generalization is unproven. Preserve both `build/protocol-study-20261026-r1/`
  and `r2/`, their frozen sources and model. They describe the **older firmware**.
  Do not relabel those runs or tune on their test outcomes. No model was retrained
  or deployed during the runtime correction.
- The [post-fix study](ai/qualification/protocol-study-shared-20261026.json) is
  complete: 96 fresh runs, 24,576 observations, 48 labelled candidates and a
  seven-node host model. Four evaluation workloads were reserved before capture
  and were absent from the first study. It made no wrong recommendations but
  unnecessarily declined the passing 2 MHz SPI workload; its predeclared coverage
  criterion failed. The [decision analysis](ai/qualification/protocol-analysis-shared-20261026.json)
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
- Read [remaining work](#remaining-work), [bench evidence](firmware/qualification/README.md)
  and [local artifacts](docs/README.md#local-artifacts-and-review-documents).
  Current build snapshots, serial logs, before/after captures and backup reference
  are retained under `build/shared-handoff-20261026/`.

## Current status

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
historical evidence are indexed in [firmware qualification](firmware/qualification/README.md)
and [AI results](ai/README.md#results-and-remaining-work).

## Platform contract

```text
profile + reviewed catalogue + board/build capabilities
    -> complete validation and resource plan
    -> driver instances and generic runtime workers
    -> typed samples, display, health and bounded output rules
```

- Existing compiled drivers can be enabled, repeated and configured without
  rebuilding the generic runtime. Each instance has its own identity and state.
- A new descriptor describes capabilities; it cannot install a missing driver.
  New driver logic requires implementation, a registry entry and a firmware build.
- Host and firmware both validate the full profile. Disabled devices still
  reserve physical wiring. Invalid candidates must preserve the working plan.
- Device identity, protocol, pins and electrical limits come from reviewed
  catalogue facts and explicit confirmation. Discovery is not arbitrary hot-plug
  support, a GPIO scan or AI-generated hardware identification.
- AI may propose an interval within supported limits. It cannot bypass normal
  validation or silently apply a recommendation.

| Contract | Version / bound |
|---|---|
| Node profile | 2.1.0; 2.0.0 input retained; schema 1.0 rejected |
| Descriptor, board, build and normalized host plan | 1.0.0 |
| Discovery / timing experiments, bundles and datasets | 0.1.0; live timing runs use 0.2.0, independently of the legacy AI API |
| Configured instances / active peripheral workers | 8 / 4 |
| Queues | 4; depth at most 8; items at most 64 bytes |
| Peripheral task stacks | 32 KiB permanently reserved; admission still limits aggregate requested budgets to 16 KiB; other memory is additional |
| Application priorities | 5..19; smaller numbers have higher urgency |

Each FIFO admits at most one enabled consumer and may have multiple producers.
The host manifest is a test fixture; actual deployment uses the connected
firmware's capabilities. An active configuration does not prove healthy readings.
See [shared contracts](firmware/CONTRACTS.md) and [admission](firmware/src/config/README.md).

## Team responsibilities

These are the original contribution areas. The user has authorized the current
completion effort to implement and integrate outstanding work across all five.
Keep teammate attribution and existing contributions; use the delivery gates
below to determine what is complete.

| Member | Area | Remaining acceptance focus |
|---|---|---|
| 1 | Kernel, runtime, task/resource ownership and health | Combined load, recovery, timing and memory |
| 2 | I2C/SPI adapters and drivers | Sensor/display wiring, bus behavior and faults |
| 3 | UART/GPIO, radar, distance and PWM | Radar absence/fault distinction, distance and safe outputs |
| 4 | Discovery specifics and compact AI recommendations | Real measurements, independent evaluation, useful embedded inference |
| 5 | Profiles, local UI, transfer/storage and system integration | Review/apply flow, persistence and end-to-end acceptance |

B-LUX, W25QXX and encoder support are possible extensions, not completed drivers.
The earlier Member 5 toolkit is preserved in [legacy/member5](legacy/member5/README.md);
its S3/schema-1.0 assumptions do not apply to the current C3 configurator.

## Remaining work

The following is the current consolidated implementation plan. It supersedes
the earlier Member 4-only sequence without changing historical evidence.

### Overall delivery plan

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
on demand. See the [wireless integration gate](firmware/README.md#on-device-wireless-integration-gate).
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

## Documentation convention

Use one main guide per maintained folder, normally `README.md`. Update the
existing guide rather than adding a checkpoint diary. Keep status, usage and
current contracts; put measured results in dated evidence records. Historical
paths and records are explained in [the documentation index](docs/README.md#historical-references).
Root coding rules, the hardware guide, shared firmware contracts and signed
audit records remain separate because they have distinct responsibilities.
Vendor, license and preserved legacy documentation retain their original layout.
