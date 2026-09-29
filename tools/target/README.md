# Kernel qualification tooling

`qualify_kernel.py` builds `examples/tasks` from the pinned kernel, optionally
applying an explicit reviewed compatibility patch to the isolated source copy.
It checks the kernel/SDK revisions, clean checkouts, SDK submodule revisions,
compiler package and available tools before building in a new directory without
spaces. It stages a Git archive, so the project submodule remains unchanged.
The child build uses forward slashes in `IDF_PATH` for the upstream CMake files
and `PYTHONUTF8=1` for Windows logs; no system environment settings are changed.

From the Code repository root, activate the isolated installation created for
this project (or your own installation of exactly the pinned versions):

```powershell
$env:IDF_TOOLS_PATH = "$env:USERPROFILE\esp\uthome-c3-tools"
. "$env:USERPROFILE\esp\uthome-c3-sdk-v6.1\export.ps1"
python tools/target/qualify_kernel.py inspect
python tools/target/qualify_kernel.py build --work-dir "$env:USERPROFILE\esp\uthome-kernel-qualification-new-run" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch
```

Choose a fresh work directory on each run. Existing destinations are rejected,
including empty ones; nothing is cleaned or overwritten. Build output is also
forbidden inside either dependency checkout. `--sdk` and `--kernel` can select
other local copies, but their revisions must still match the pins. Omit
`--compat-patch` to reproduce an unmodified upstream build. The selected release
requires the recorded compatibility patch: the upstream component definitions
omit the `esp_hal_systimer` dependencies needed by ESP-IDF 6.1. This was observed
in a real build, not inferred from host tests.

Outputs include the pristine source archive, staged source tree, build log, generated
sdkconfig, firmware images, and `evidence.json` with versions, command, exit code,
artifact sizes and SHA-256 hashes. An applied compatibility patch is copied into
the run directory and identified by its own SHA-256. A failed build retains its diagnostic report
and log. `board_status` stays `not_tested`: compilation does not prove boot,
timing or hardware compatibility. This tool never opens a serial port or flashes.

## Board qualification after compilation

Use a known USB data cable and disconnect peripheral wiring for the untouched
upstream example. Its board initialization also starts native UART/ADC drivers.
Check actual USB identities with:

```powershell
python -m serial.tools.list_ports -v
```

A Bluetooth COM port is not the ESP32. The photographed C3 SuperMini should use
native USB-Serial-JTAG; identify the actual port and chip before selecting images.
The SDK's esptool can report chip and flash information without writing firmware:

```powershell
python -m esptool --port COMX chip-id
python -m esptool --port COMX flash-id
```

If the board has power but no USB device appears, try a known data cable and a
direct USB port. With peripherals disconnected, hold BOOT, press/release RST,
then release BOOT after a second to enter ROM download mode. Espressif documents
[GPIO9-low plus reset](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-guides/usb-serial-jtag-console.html)
as the recovery path when application firmware disables the USB interface.
A power LED alone does not establish a working USB data connection.

Replace `COMX` with the identified board port. These commands may reset the board.
Verify ESP32-C3 and at least the configured 4 MB of flash. Once the selected
firmware and connected board are confirmed, flash from that run's build folder
using its generated `flash_args`, or use the exact recorded idf.py build
arguments with `-p COMX flash monitor`. Preserve the serial log for the startup
handover, kernel banner, repeated A/B ticks, and a reboot check.

The 2026-09-28 board run used COM8 and verified 4 MB flash. A full backup was
saved before writing the bootloader, partition table and task image. Its first
USB reset stayed in download mode; esptool 5.4.0 watchdog reset recovered normal
boot. Subsequent USB resets produced complete startup logs. See
[the board record](../../firmware/qualification/esp32c3-board-20260928.json) and
[reset recovery instructions](../../firmware/SETUP_ESP32C3.md). Keep the original
build record unchanged: board evidence is recorded separately after testing.

The kernel task example is a qualification artifact, not the uT-Home application.
Target runtime worker integration and peripheral adapters require separate
acceptance evidence. See [the setup guide](../../firmware/SETUP_ESP32C3.md) and
[the runtime integration record](../../firmware/src/runtime/INTEGRATION.md).

## Tool checks

```powershell
python -B -m unittest discover -s tests/target -v
```

These tests exercise rejected revisions, dirty sources, unavailable tooling,
protected build destinations, archive paths and retained failure evidence.
They use no physical board and do not claim a target build succeeded.

## Project runtime task-port test

After qualifying the upstream example, build the project-owned adapter and
board acceptance test with the same pinned SDK and compatibility patch:

```powershell
python tools/target/qualify_kernel.py build --work-dir "$env:USERPROFILE\esp\uthome-runtime-qualification-new-run" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch --runtime-port
```

The helper snapshots the adapter, activation/shared modules, their
component/queue/health/watchdog dependencies, headers and board test under `project/`,
records every source hash, and builds `uthome_runtime_port.bin`. The vendor
checkout stays pristine. This test preserves the upstream example's 32 KiB
kernel heap. The explicit resource patch supplies 512 usable UART bytes and
disables native ADC startup; it is copied, checked, applied only in the isolated
source and hashed separately from the compatibility patch. Omitting that option
retains upstream resource defaults; a 512-byte UART request will then be rejected.
The fixture does not activate peripherals. Follow the same identification,
backup, flash and capture procedure; expect `[activation-test] PASS` and
`[port-test] PASS` followed by `alive` counters, with no `FAIL`. Capture a
repeat reset too. This qualifies the typed periodic activation API with test
components; it does not add JSON/USB deployment or populate the target driver list.

The peripheral software build also snapshots I2C/SPI, shared pin ownership,
C3 backends, BMP388/OLED/UART/radar modules and the pinned Bosch sources. The
manifest now contains 59 recorded inputs including kernel defaults. Argument calls
retain these functions in the linked image without GPIO activity. A successful
build does not qualify electrical transfers; new build evidence remains separate
from the existing activation board record.

Qualification inspects `compile_commands.json` after the build, requiring C99
for ordinary owned modules and C11 only for the two approved atomic paths.
The exact GNU99 paths are `c3i2c.c`, `c3spi.c` and `c3pin.c`, whose pinned external
LL headers use GNU `typeof`; their owned source remains C99.
Source-specific options follow the SDK defaults; late SDK language overrides or
remaining `-Wno-error` exemptions fail qualification. The compiler-profile
results and command-database hash are retained in the build record.

## Configuration application build

Add `--configuration-app` to `qualify_kernel.py build --runtime-port` to build
the USB/profile/storage application instead of running the acceptance fixture.
The option is rejected without `--runtime-port`. It selects an additional
dedicated configuration partition; no existing board is modified by building.
See [exact layout, startup and deferred hardware acceptance](../../firmware/src/config/USB_STORAGE.md).
