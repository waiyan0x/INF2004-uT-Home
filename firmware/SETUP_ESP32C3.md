# ESP32-C3 SuperMini kernel baseline

## Selection and current status

| Item | Selection |
|---|---|
| Physical board | ESP32-C3 SuperMini, identified from the supplied board photographs |
| Kernel port | https://github.com/sirfonzie/esp32c3-mtk3.git |
| Pinned commit | `ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` |
| Repository path | `firmware/third_party/esp32c3-mtk3/` (Git submodule) |
| ESP-IDF target | `esp32c3` |
| Board kit | `iote_esp32c3_mini` |
| CPU / console | Single-core RISC-V / native USB-Serial-JTAG |
| First build | `examples/tasks`, kernel only, with recorded SDK compatibility patch |
| Flash capacity | 4 MB detected on COM8 on 2026-09-28; matches upstream defaults |
| Selected ESP-IDF | v6.1, commit `fff9895c82d744c7237be8847347bdd1b07c6643` |
| Selected compiler | `riscv32-esp-elf`, package `esp-15.2.0_20251204` |
| Qualification status | Upstream task example and project periodic-worker acceptance passed on 2026-09-28 |
| μT-Home target integration | Pending; current host tests do not establish target operation |

This independent port is selected for the project. Its source includes the
kernel and headers; downloading TRON's generic source separately is unnecessary.
Keep the dependency unchanged; apply the tracked compatibility patch only to a
separate build copy. The task example compiles and runs on the connected C3.
Two complete 25-second captures show the startup handover, kernel banner and
alternating A/B counters 0 through 24 after USB resets. See
[the qualification record](qualification/README.md) for evidence and limits.

## 1. Obtain the pinned source

From the μT-Home repository root:

```powershell
git submodule update --init --recursive firmware/third_party/esp32c3-mtk3
git -C firmware/third_party/esp32c3-mtk3 rev-parse HEAD
```

Check the hash against the selection table. New project clones can use
`git clone --recurse-submodules` to populate the dependency automatically.
Use the committed submodule pointer rather than following upstream `main`.

## 2. Activate ESP-IDF and choose a build directory

Use Espressif's [Windows installation instructions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/windows-setup.html)
and open its activated PowerShell terminal. Check:

```powershell
idf.py --version
riscv32-esp-elf-gcc --version
git -C $env:IDF_PATH rev-parse HEAD
```

The project selects ESP-IDF **v6.1** and the tool package in the table above.
Install this release using Espressif's installer, or clone the SDK including
its dependencies in a path without spaces:

```powershell
git clone --branch v6.1 --recursive https://github.com/espressif/esp-idf.git C:\esp\esp-idf-v6.1
git -C C:\esp\esp-idf-v6.1 rev-parse HEAD
```

The hash must be `fff9895c82d744c7237be8847347bdd1b07c6643`. Install its prescribed
tools and activate its terminal before the commands above. Upstream used
6.1-era development sources; this released version is our selected SDK. Its
task-example build and board startup/reset checks passed with the recorded
compatibility patch. Record compiler/SDK versions and build/boot output for each
new run. A demonstrated incompatibility triggers a documented patch or version
change rather than silently switching SDKs.

ESP-IDF [does not support spaces in project or SDK paths](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/windows-start-project.html).
Our current `INF2004 - Embedded Systems Programming` project path contains
spaces. Build the baseline in a separate checkout such as
`C:\esp\uthome-c3-baseline`, with the exact same pinned source:

```powershell
git clone https://github.com/sirfonzie/esp32c3-mtk3.git C:\esp\uthome-c3-baseline
git -C C:\esp\uthome-c3-baseline checkout --detach ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2
Set-Location C:\esp\uthome-c3-baseline
git rev-parse HEAD
```

If that directory already exists, inspect its origin, revision, and local
changes before reusing it. This external checkout is for upstream baseline
validation; it is not the μT-Home application or a new place for team source.

## 3. Build the task example

For repeatable qualification, prefer the project helper from an activated SDK
terminal at the Code repository root:

```powershell
python tools/target/qualify_kernel.py inspect
python tools/target/qualify_kernel.py build --work-dir "$env:USERPROFILE\esp\uthome-kernel-qualification-new-run" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch
```

The destination must be new and have no spaces. This checks the exact source and
tool pins, stages the pinned kernel outside the project, and records logs and
artifact hashes. The explicit compatibility patch supplies the CMake dependencies
missing from the unmodified port under ESP-IDF 6.1. Its hash is recorded too. It normalizes `IDF_PATH` to forward slashes in the build child
process because the upstream CMake files interpolate that path. See
[the target tooling guide](../tools/target/README.md). Manual commands follow for
reference; preserve the same path normalization on Windows.

For a manual build, first apply
`firmware/third_party/patches/0002-esp-idf-6.1-build.patch` to the separate baseline
checkout with `git apply --check` followed by `git apply`. Keep the project
submodule unchanged. Then use an activated ESP-IDF PowerShell session:

```powershell
$env:IDF_PATH = $env:IDF_PATH.Replace('\', '/')
$env:PYTHONUTF8 = '1'
```

```powershell
idf.py -C examples/tasks -DMTK_BOARD=iote_esp32c3_mini set-target esp32c3
idf.py -C examples/tasks -DMTK_BOARD=iote_esp32c3_mini build
```

Use `set-target` once for a fresh example checkout; it regenerates build
configuration. Keep the example's `sdkconfig.defaults`, which selects C3,
4 MB flash and native USB-Serial-JTAG. Start with peripherals disconnected.
The kernel's board initialisation also brings up its own serial/ADC drivers;
the two-task example avoids the root demo's extra peripheral exercises.

Check Windows Device Manager for the actual COM port and identify the chip
and flash before flashing. With the esptool supplied by ESP-IDF, inspect
`python -m esptool --help`: current releases use `chip-id` / `flash-id`, while
older releases spell them `chip_id` / `flash_id`. These identification commands
do not write firmware, but may reset the connected board.

After checking the chip and 4 MB flash, use that board's actual port (`COM8`
on the qualification machine). Save existing firmware before replacing it:

```powershell
idf.py -C examples/tasks -DMTK_BOARD=iote_esp32c3_mini -p COM8 flash monitor
```

Expected evidence: the startup log reports the FreeRTOS startup bypass,
μT-Kernel starts, and tasks A/B repeatedly print alternating ticks. Record
the serial log and reboot result; a successful compilation alone does not
qualify the baseline. Exit the monitor with Ctrl+].

During the recorded run, a USB reset initially left the chip in ROM download
mode. The installed esptool 5.4.0 watchdog reset started the application; later
USB resets booted normally. With no monitor holding the port, the equivalent
recovery command is `python -m esptool --chip esp32c3 --port COM8 --after watchdog-reset flash-id`.
It reads flash identification and resets the chip; it does not write firmware.
Open the monitor again afterward. Early output can be lost during the watchdog
reset, so capture a subsequent normal reset for the full startup log.

The upstream root demo also includes kernel API smoke tests. Run it as a
separate qualification step with peripherals disconnected, following its
`docs/GETTING_STARTED.md`; its advertised results are not our test results.

## 4. Member 1: source and integration points

All paths below are relative to the pinned dependency:

| Path | Use |
|---|---|
| `include/tk/tkernel.h` | Public kernel umbrella header |
| `include/tk/syscall.h` | `T_CTSK`, task APIs and related types |
| `include/tk/typedef.h` | Kernel IDs, errors, timeout and size types |
| `config/config.h` | Priority/object limits and 1 ms tick configuration |
| `components/mtkernel/CMakeLists.txt` | Include paths, board definitions and linker wrappers |
| `components/mtkernel/mtk_startup.c` | Startup handover and default 32 KiB kernel heap |
| `examples/tasks/main/main.c` | Task entry signatures, idle task and `usermain()` |
| `kernel/tkernel/task_manage.c` | Creation, start, termination, deletion and priority behaviour |

Use `#include <tk/tkernel.h>` through the real `mtkernel` component. Do not
copy individual headers into `firmware/include/uthome/`; their types and build
definitions must remain consistent with the implementation.

The port replaces ESP-IDF's FreeRTOS scheduler at startup. Application startup
is `EXPORT INT usermain(void)`, as in the task example. Preserve the required
idle task and startup handover when wiring the μT-Home application.

The project periodic-worker implementation is now in
`firmware/src/runtime/tkport.c`, with its explicit contract in
`workport.h`. The board acceptance test passed twice; see
[the task-port evidence](qualification/esp32c3-runtime-20260928.json).
It uses one lifecycle owner and retains task/context ownership on failed cleanup.
Whole-plan activation, live replacement and shared queue/health state remain
separate integration work. The kernel API mapping is:

- Prepare: populate `T_CTSK` and call `tk_cre_tsk`; retain the returned ID only
  on success. `stksz` is in bytes. The example uses `TA_HLNG | TA_RNG0`.
- Start: call `tk_sta_tsk` on the prepared task. Starting a higher-priority
  task can preempt the caller immediately; publish its context first.
- Stop: define cooperative cancellation and bounded cleanup of drivers and
  owned resources. `tk_ter_tsk` terminates another task; `tk_del_tsk` removes
  a dormant task. They are distinct operations, and forced termination is not
  a substitute for application cleanup. Handle self-exit separately.
- Reconfigure: distinguish values that can change in place from stack-size
  changes, which require controlled recreation. Check every kernel result.
- Priorities: the pinned configuration allows 1..32, with smaller numbers
  higher. The example uses 32 for idle. Node configuration may use application
  priorities 5..19; service priorities remain reserved by the board policy.

Do not treat kernel maximum task count or its 32 KiB object/stack heap as the
application device-task limit or AI budget. Account for kernel/internal tasks,
all stacks and queue allocations, and measured free application memory.
The port's interrupt watchdog is active; the ESP-IDF Task-WDT is disabled in
its defaults. Runtime heartbeat/watchdog integration still needs Member 1.

## 5. Application baseline after the upstream example

Follow [PROJECT_BASELINE.md](../PROJECT_BASELINE.md) for the selected settings
and acceptance tests. UART1 stays with the native serial driver behind Member
3's single-owner HAL adapter. Apply the tracked
[resource patch](third_party/patches/README.md) in the separate integration
checkout to obtain 512 usable serial-buffer bytes and disable unused native
ADC startup. Keep the submodule itself clean.

The application then needs a 64 KiB kernel-heap startup override, real HAL
implementations, validated instance scheduling, bounded USB/configuration handling,
and measured AI resource use. These choices are now specified; implementation
and application-level board qualification remain outstanding. Branches not yet integrated must
be reconciled with the C3 baseline, especially Member 5's schema/generator.
