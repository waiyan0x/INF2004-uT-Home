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
startup handover. `tkport` owns permanent worker bindings, start and cooperative stop/release;
`activate` owns whole-plan replacement. Forced termination is not cleanup.

UART1 remains owned by native `DEV_SER`; the HAL wraps it rather than installing
a competing driver. The resource patch provides 512 usable receive bytes and
disables unused native ADC startup. The native serial records use fixed typed
storage; the kernel's internal pool is separate from application memory and is
not an AI budget. Pool size depends on build mode: use the ELF/map and the
selected [budget](../tools/target/README.md#fixed-memory-budget), not an old size.

Kernel headers are under `third_party/esp32c3-mtk3/include/tk/`; configuration,
startup and task implementation are in that dependency's `config/`,
`components/mtkernel/` and `kernel/tkernel/`. Pin/wiring constraints are in
[configurations](../configs/README.md), and exact limits remain in source/catalogue.

## On-device wireless integration gate

Wi-Fi and BLE must run on the ESP under uT-Kernel. The planned services are BLE
identification/status/telemetry and Wi-Fi MQTT telemetry plus bounded HTTP
status/configuration. They are not integrated into the ordinary USB application.
The [network guide](src/network/README.md) owns compatibility contracts and the
[source audit](qualification/esp32c3-wireless-source-audit-20261027.json) identifies
remaining task, notification, timer, TLS/per-task-state and SDK allocation gaps.

The optional `--static-net-queues` build selects owned fixed queue/event resources;
it does not enable radios or prove whole-system static compliance. Queue and
event adapters are implemented; task stacks, notification semantics, timer
callback context, BLE/lwIP/service allocation and cache-off traffic still need work.
Isolated Wi-Fi scan/restart and read-only GATT evidence is indexed in
[qualification](qualification/README.md).

Preserve the pinned SDK/kernel. Use owned wrappers and isolated build integration,
not vendor edits or a switch to FreeRTOS scheduling. An SDK API named `Static`
may still allocate or ignore caller storage. Check actual symbols, stack-size
units, callback/ISR context, priority/TLS behavior and compiled tick frequency.
The inspected setup uses 1000 Hz ticks and a native 1 ms timer; stale comments
do not establish a tick mismatch.

Inventory all infrastructure tasks, stacks, objects and buffers separately from
the four peripheral slots. Define bounded connections, payloads and queued work;
precreate resources needed for reconnects. Audit BLE, lwIP, MQTT, HTTP, credential
storage and any TLS path, not just the Wi-Fi driver. Do not copy upstream demos'
automatic NVS erase or open-access settings into the application.

Network callbacks must send bounded requests to the existing controller owner.
Keep authentication, validation, explicit reviewed Apply and truthful save/fault
responses. AP/broker/client loss must not stop sampling, local rules or recovery.
Exercise concurrent traffic, full queues, oversized messages, client exhaustion,
reconnects and flash saves while measuring allocations, object counts and timing.
The queue adapter rejects paths requiring flash code while cache is disabled;
successful normal traffic alone does not establish save-time radio compatibility.

## Mandatory static-memory change

The supplied `Project/ESP/Tribunal1/requirement_change.txt` prohibits application
heap allocation, including startup, requires kernel objects at startup only,
and requires fixed per-module RAM budgets within a declared total. It covers
parsing, validation, Apply/rollback, communication, supervision and temporary
buffers. Oversize input and exhausted capacity must fail without changing the
working plan or falling back to heap storage.

The document allows a justified Wi-Fi-driver exception; no blanket SDK, BLE,
TCP/IP, TLS, libc, UART or application exemption is established. Any claimed
exception needs explicit scope and evidence. Startup object creation is not
permission for application `malloc`.

### Confirmed changes and dependency boundaries

The USB core uses fixed parser pools, four permanent caller-owned 8 KiB worker
stacks, a fixed 1 KiB idle stack, startup HAL locks, retained native UART resources
and checked nonallocating profile storage. See [parsing/storage](src/config/README.md),
[workers/audits](src/runtime/README.md) and [UART storage](../docs/uart-static-storage.md).
No application-startup/runtime allocation or late-object calls were observed in
the scoped core acceptance. Wireless and complete-system qualification remain open.

Preserve these failure protections: aligned parser storage and partial-parse
cleanup; cooperative worker quiescence before rebinding; retained ownership after
failed cleanup; no implicit retry of failed startup creation; checked partition
layout before writes. SDK partition lookup can allocate, and flash reads may
use a heap buffer for unsuitable destinations. Audit reachable calls, not just
application symbols; source grep alone proves neither execution nor absence.

### Reusable design and measured baseline

Use the current ELF/map and [budget tooling](../tools/target/README.md#fixed-memory-budget).
Count fixed controller/plans, parser, caches, queues, driver storage, kernel/SDK
reservations, RAM code and stacks. Four physical worker stacks reserve 32 KiB;
the separate aggregate requested-stack admission limit remains 16 KiB. Neither
active worker count nor a logical queue depth measures total reserved RAM.

The exact storage-layout guard permits only the two profile slots. It does not
authorize OTA, NVS or network credential writes. Any layout/persistence change
requires reviewed bounds, backups and preservation/migration of configuration.
The frozen byte/ELF measurements are in the [evidence log](qualification/README.md),
not a reusable promise of free memory in later builds.

### Migration and acceptance order

1. Finish fixed wireless service resources and actual SDK semantics, with budgets
   and host/target capabilities kept consistent.
2. Verify maximum occupancy, parser/queue/storage exhaustion, discovery cleanup,
   rollback, network loss and flash saves on the combined image. Count creation
   calls, not just live objects, and retain unavailable measurements as null.
3. Run the full C gate, manual review, warning-clean tests and real-target builds.
   Preserve map attribution, stack evidence and scoped board results.
4. Freeze that execution context for a new AI study. An eligible model, versioned
   advice transport and loaded inference/sampling comparison are still required.

The memory rule applies to the embedded system; desktop training and the Desk
are host processes. Keep normal validated configuration when AI abstains. UI
changes require review; the memory change itself does not require a redesign.
