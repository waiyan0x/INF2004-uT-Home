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
disables unused native ADC startup. The application has a 64 KiB kernel-heap
override; do not confuse this with free application RAM or an AI budget.

Kernel headers are under `third_party/esp32c3-mtk3/include/tk/`; configuration,
startup and task implementation are in that dependency's `config/`,
`components/mtkernel/` and `kernel/tkernel/`. Pin/wiring constraints are in
[configurations](../configs/README.md), and exact limits remain in source/catalogue.
