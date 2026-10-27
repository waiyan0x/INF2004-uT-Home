# Firmware setup and structure

The configuration application binds validated profiles to independent driver
contexts, generic kernel workers, typed samples, telemetry and output rules.
It includes reviewed discovery. Software/build checks and basic peripheral
bench observations exist; full physical acceptance is still pending.

## Pinned target

| Item | Selection |
|---|---|
| Board / CPU / console | ESP32-C3 SuperMini / single-core RISC-V / native USB-Serial-JTAG |
| Kernel port | [sirfonzie/esp32c3-mtk3](https://github.com/sirfonzie/esp32c3-mtk3) |
| Kernel revision | `ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` |
| ESP-IDF | v6.1, `fff9895c82d744c7237be8847347bdd1b07c6643` |
| Compiler package | `riscv32-esp-elf`, `esp-15.2.0_20251204` |
| Target / board kit | `esp32c3` / `iote_esp32c3_mini` |
| Flash | 4 MB confirmed on the tested C3; identify each board before flashing |

## Build

Run from the repository root. Obtain the exact submodule; do not follow upstream
main or copy individual kernel headers into project includes.

```powershell
git submodule update --init --recursive firmware/third_party/esp32c3-mtk3
git -C firmware/third_party/esp32c3-mtk3 rev-parse HEAD
```

Use the pinned ESP-IDF installation and its activated terminal. A new SDK setup
can follow [Espressif's Windows guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/windows-setup.html),
but its revision/toolchain must match the table. SDK and build paths must have
no spaces. The [build helper](../tools/target/README.md) stages sources in a fresh
directory, checks revisions/compiler profiles, applies tracked patches only to
the staged copy and records evidence. Keep the vendored checkout pristine.

```powershell
$env:IDF_TOOLS_PATH = "$env:USERPROFILE/esp/uthome-c3-tools"
. "$env:USERPROFILE/esp/uthome-c3-sdk-v6.1/export.ps1"
python tools/target/qualify_kernel.py inspect
python tools/target/qualify_kernel.py build --runtime-port --configuration-app --work-dir "$env:USERPROFILE/esp/uthome-config-new-build" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

- Both flags build the **configuration application** used with peripherals/UI.
- Omit `--configuration-app` for the runtime acceptance fixture.
- Omit both flags for the upstream two-task kernel example.
- Build success is not board acceptance. These commands do not flash.

Identify the current COM port, preserve a full flash backup and verify image
hashes/partition layout before intentional flashing. Use the
[target procedure](../tools/target/README.md#install-the-configuration-application)
and [hardware acceptance guide](../CONTRIBUTING.md). COM8 is historical, not a
required port. A quiet terminal is normal once binary application traffic owns USB.

## Module map

| Folder | Responsibility |
|---|---|
| `app/` | Startup and single controller owner |
| `include/uthome/` | Shared component, registry and sample APIs; [contracts](CONTRACTS.md) |
| `src/config/` | Independent admission, binding, USB framing and persistent profiles |
| `src/runtime/` | Host lifecycle model, kernel workers, activation and recovery |
| `src/discovery/` | Bounded I2C/UART observations |
| `src/services/` | Latest-sample cache, telemetry, health service and OLED rendering |
| `src/automation/` | Bounded typed sensor-to-PWM rules |
| `src/messaging/`, `src/health/` | Queues, progress/error reporting and watchdogs |
| `drivers/`, `hal/` | Device behavior and reusable protocol/board adapters |
| `third_party/`, `qualification/` | Pinned dependencies and dated evidence |

## Kernel integration constraints

Use `<tk/tkernel.h>` through the actual `mtkernel` component. The port replaces
the ESP-IDF scheduler at startup; retain `usermain`, the idle task and native
startup handover. `tkport` owns task prepare/start/cooperative stop/delete;
`activate` owns whole-plan replacement. Forced termination is not cleanup.

UART1 remains owned by native `DEV_SER`; the HAL wraps it rather than installing
a competing driver. The resource patch provides 512 usable receive bytes and
disables unused native ADC startup. The retained 26 October application ELF
contains the upstream **32 KiB** `mtk_default_heap`; the earlier guide's 64 KiB
override claim was incorrect. This pool is neither free application RAM nor an
AI budget. Its allocation lifecycle also needs the review below.

Kernel headers are under `third_party/esp32c3-mtk3/include/tk/`; configuration,
startup and task implementation are in that dependency's `config/`,
`components/mtkernel/` and `kernel/tkernel/`. Pin/wiring constraints are in
[configurations](../configs/README.md), and exact limits remain in source/catalogue.

## On-device wireless integration gate

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

An owned [fixed queue adapter](src/network/README.md) is now under isolated
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
see the [overall delivery plan](../README.md#overall-delivery-plan).

## Mandatory static-memory change

The overall completion scope now also requires **on-device Wi-Fi and BLE**.
Include the [wireless integration gate](#on-device-wireless-integration-gate)
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
backup. Its [bench regression](qualification/esp32c3-integration-bench-20261026.json)
passed twenty discovery/profile cycles and a separate failed-activation rollback;
the original settings were restored at revision 179. This does not establish
static-memory compliance.

The 27 October implementation reserves four 8 KiB caller-owned worker stacks,
uses typed fixed cJSON pools (28,672 pool bytes on C3, plus 26 control bytes), and
replaces allocating storage lookup/protection paths with an exact checked layout.
See [configuration](src/config/README.md#static-partition-access),
[worker lifecycle](src/runtime/README.md#permanent-target-workers) and the
[fixed-budget tool](../tools/target/README.md#fixed-memory-budget).
The ordinary runtime fixture passed 909 checks on two boots; the new application
passed twenty discovery/profile/save cycles and failed-activation rollback, then
restored revision 260 / CRC 3623560383 after reset with fresh BMP388/radar readings.
These results do not certify whole-system zero allocation or wireless operation.
The subsequent [allocation audit](qualification/esp32c3-allocation-audit-20261027.json)
adds boot/application-startup/normal counters and a fixed 1 KiB idle stack.
On its final image, application startup and normal operation observed zero SDK
heap events and kernel allocator entry attempts; all post-startup object counters
remained zero through twenty cycles, rollback and reboot restore at revision 342.
The fixed internal-RAM footprint is 230,190 bytes within the 278,528-byte budget.
Hook coverage, deferred stack measurements and wireless limits remain explicit.
The [integration record](qualification/esp32c3-system-integration-20261026.json)
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

### Confirmed changes and dependency boundaries

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

### Reusable design and measured baseline

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

### Migration and acceptance order

The user has authorized this effort to implement all remaining subsystem work.
The [overall delivery plan](../README.md#overall-delivery-plan) is the canonical
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
   [AI adaptation plan](../ai/README.md#ai-plan-for-the-agreed-static-memory-design)
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
