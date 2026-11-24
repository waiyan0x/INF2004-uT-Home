# Firmware setup and structure

The configuration application binds validated profiles to independent driver
contexts, generic kernel workers, typed samples, telemetry and output rules.
It includes reviewed discovery. The single-broker BMP388/LD2450 demo has
[scoped physical verification](../tools/configurator/README.md#physical-verification).
Other hardware acceptance items remain in the [acceptance guide](../HARDWARE_ACCEPTANCE.md).

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
and [hardware acceptance guide](../HARDWARE_ACCEPTANCE.md). COM8 is historical, not a
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
| `third_party/` | Pinned dependencies |

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

## Wireless integration and static memory

The qualified delivery uses uT-Kernel, one authenticated MQTT/TLS broker and the
BMP388/LD2450 transaction workload. Full saved-profile setup and recovery use
USB. Optional BLE and radio diagnostics are separate build modes; they do not
inherit the Wi-Fi demo qualification. ESP HTTPS is retired.

The [AAI13 assessment](../docs/reports/wifi-static-memory/README.md) records the
release's fixed RAM, zero observed application-owned general allocations on the
qualified workload, zero normal-service kernel object creations, and the scoped
Wi-Fi driver exemption. It also identifies pre-application SDK allocations and
the forced USB/certificate-verification overlap that remains outside acceptance.

The core uses bounded parser/storage workspaces, permanent worker stacks,
startup HAL locks and retained native UART resources. Network adapters reserve
fixed task, queue, event, packet, timer and TLS storage. Read the
[network contracts](src/network/README.md), [runtime contracts](src/runtime/README.md)
and [storage policy](src/config/README.md) for their lifetimes and failure paths.

Keep SDK/kernel pins and owned wrappers. Do not modify vendor dependencies,
raise budgets or add heap fallbacks. A static-looking SDK API alone does not
prove allocation behavior. Apply/rollback must preserve ownership until cleanup
succeeds; rejected inputs must leave the working plan intact.

## Build and acceptance boundaries

Run the pinned C gate, manual review, affected warning-clean tests and real-target
qualification after relevant changes. Reproduce the module and aggregate budget
with the current ELF/map. Fixed linked storage, runtime live heap, stack margin
and timing are different measurements. Record source/build identity with each
physical result; a previously built image is not proof of the currently installed
firmware.

The [presenter guide](../tools/configurator/README.md#tribunal-2-presenter-guide)
covers runtime rule switching without reflashing. Broader workload, electrical,
optional peripheral and interrupted-storage tests follow the
[hardware acceptance procedure](../HARDWARE_ACCEPTANCE.md).
