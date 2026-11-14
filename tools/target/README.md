# Target build and qualification tools

`qualify_kernel.py` builds an isolated archive of the pinned kernel with reviewed
patches. It checks clean kernel/SDK revisions and SDK submodules, compiler/tools,
source hashes and compiler profiles. The dependency checkouts stay unchanged.
No build command opens serial, resets or flashes the board.

Post-build policies live in named `verify_*` functions. In particular,
`verify_wifi_probe` checks diagnostic settings, `verify_configuration_layout`
checks the reviewed partition bytes, and `verify_managed_cjson` checks the lock
version and host/target source identity. The build coordinator sequences these
checks and retains their evidence; a successful compiler exit is insufficient.

## Build modes

The CLI keeps the flags below. `build_modes.py` resolves them before any staging
into one immutable `BuildSelection`, with independent static-queue and BLE
options. Its mode table owns prerequisites; the selection emits both CMake
settings and evidence flags. Python callers pass `selection=resolve_mode(...)`
to `build()`, with compatibility/resource patches supplied by keyword.

Activate the pinned [firmware environment](../../firmware/README.md#pinned-target),
then run `python tools/target/qualify_kernel.py inspect` from the repository root.
Use a fresh, nonexistent output directory without spaces. Existing destinations
and paths inside dependencies are rejected, never cleaned. Custom `--sdk` and
`--kernel` copies must match the pins.

| Flags | Image purpose |
|---|---|
| Neither mode flag | Upstream `examples/tasks` two-task kernel example |
| `--upstream-example ble_gatt` | Pinned BLE GATT diagnostic; excludes application/runtime flags |
| `--upstream-example udp_pingpong_c3` | Pinned Wi-Fi/UDP diagnostic; excludes application/runtime flags |
| `--runtime-port` | Project acceptance fixture; no peripheral activation |
| `--runtime-port --native-uart-fixture` | Isolated native UART ring/fault checks; disconnect external UART senders and receivers before installation |
| `--runtime-port --configuration-app` | USB/profile/storage application used with the UI and peripherals |
| `--runtime-port --static-net-queues --senlux-probe` | Isolated SEN0390 and BMP388 read diagnostic; no application workers |
| `--runtime-port --configuration-app --static-net-queues --senlux-app-probe` | Diagnostic configuration application with the provisional SEN0390 binding enabled |

Diagnostic examples retain their pinned source bytes and get separate build
records marked `diagnostic_only`. They do not qualify the application, static
memory or radio coexistence. The existing source/pin/archive/output protections
also apply; unknown examples and mixed modes fail before any workspace changes.
These examples have their own startup, networking and persistence behavior;
compilation does not authorize installing them on an assembled application board.

The native UART fixture transmits bytes and assumes an empty receive path. It is
explicitly separate from the ordinary runtime fixture, which prints that UART
fault checks were not run. Neither an ordinary fixture PASS nor the connected
radar's normal operation qualifies the isolated fault suite. The build gate
rejects missing/ambiguous mode definitions and any test-hook leakage into the
application or ordinary fixture.

The configuration application also verifies its generated partition-table hash
against `cfgpart`'s reviewed layout. Both allocating SDK flash-protection queries
are replaced only in `spi_flash_os_func_app.c` with the checked configuration-only
write guard. Missing, overridden or leaked substitutions fail qualification.
NVS/OTA writes remain prohibited by this application guard; wireless persistence
requires a separately reviewed static design.

The runtime builds enable SDK heap hooks and owned `c3audit` wrappers. Generated
compiler commands must show the exact callback/creator/allocator substitutions
only in their reviewed vendor translation units. The ELF check verifies wrapper
aliases and internal-RAM addresses for heap-hook primitives. The ordinary
fixture explicitly pulls the audit object from its archive; weak SDK references
alone do not guarantee extraction. Source dependencies remain unmodified.
These checks establish instrumentation wiring, not zero application allocations.
The application reports boot totals, its startup subset (marked at `usermain`),
and normal-operation counts separately in version-3 telemetry. Restore is after
the normal-operation boundary. The fixture intentionally creates test objects;
its PASS is not an application no-allocation result.

## Configuration application build

```powershell
python tools/target/qualify_kernel.py build --runtime-port --configuration-app --work-dir "$env:USERPROFILE/esp/uthome-config-new-build" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

The compatibility patch supplies missing ESP-IDF 6.1 systimer dependencies; omit
it only to reproduce the known upstream build failure. The resource patch adds
512 usable native UART bytes and disables unused ADC startup. Patches are applied
only to the staged copy and hashed. Native startup pool size is build-dependent:
inspect the actual image map, not an earlier override claim. A pool reservation
is not measured free RAM. The
[static-memory impact review](../../firmware/README.md#mandatory-static-memory-change)
describes the outstanding allocation, lifecycle and budget changes; existing
build success does not establish compliance with the new requirement.

Outputs include `project/`, `build/`, `build.log`, `sdkconfig`, patches and
`evidence.json` with hashes/exit status. Failed builds retain diagnostics.
`board_status` remains `not_tested`. Ordinary owned units use C99, two approved
atomic paths use C11, and five SDK-header adapter boundaries use GNU99. Late
dialect overrides and warning exemptions fail qualification.

Check a staged project against current owned source before reuse:

```powershell
python tools/target/qualify_kernel.py verify-snapshot --project-dir "$env:USERPROFILE/esp/uthome-config-new-build/project"
python -B -m unittest discover -s tests/target -v
```

## SEN0390 application diagnostic

`--senlux-app-probe` requires the configuration application and static network
queues, and cannot be combined with the other probes. The build checks the
binder's actual compiler flag and marks its evidence `diagnostic_only`; ordinary
builds keep the light capability disabled. Applying a profile still writes the
saved configuration. Preserve the complete flash and exact partition layout
before this test, and restore and verify the original firmware **and** saved
configuration afterward. A short diagnostic capture is not a release sign-off.

## Identify or recover the board

Close other serial clients. Use a known USB data cable,
then `python -m serial.tools.list_ports -v`; Bluetooth ports are not the board.
Replace COMX in `python -m esptool --port COMX flash-id` with the actual port.
Confirm ESP32-C3 / 4 MB. A power LED alone does not prove USB data works.

If absent, try a direct USB port/data cable; hold BOOT, press/release RST, then
release BOOT after a second for ROM download mode. Keep GPIO9/BOOT and native
USB18/19 free. A board stuck in download mode after flashing may need BOOT
released and RESET, or esptool's watchdog reset.

## Install the configuration application

This procedure **writes firmware**. Keep serial clients closed. A board-alone
setup is useful for isolating power, boot-pin or communication failures; it is
not inherently required for a USB backup or firmware update. The reviewed
BMP388/six-pin OLED/LD2450 layout may remain assembled when its supply is stable,
boot/USB pins are free, and no actuator is connected. On 26 October the full
backup and verified update succeeded with that wiring intact. If isolation is
needed, disconnect all power before changing any signal or supply wiring.
Use a newly built, verified configuration application, not the fixture
or an arbitrary historical binary. In the activated pinned environment, set the
actual run directory and board port:

```powershell
$acceptancePython = (Get-Command python).Source
$acceptanceBuild = Join-Path $env:USERPROFILE 'esp/uthome-config-new-build'
$acceptancePort = 'COMX' # replace with the identified board port
$acceptanceEvidence = Get-Content -Raw -LiteralPath (Join-Path $acceptanceBuild 'evidence.json') | ConvertFrom-Json
foreach ($relative in @('build/uthome_runtime_port.bin', 'build/bootloader/bootloader.bin', 'build/partition_table/partition-table.bin', 'build/flasher_args.json')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $acceptanceBuild $relative) -Algorithm SHA256).Hash
    if ($actual -ne $acceptanceEvidence.artifacts.$relative.sha256) { throw "Build hash mismatch: $relative" }
}
& $acceptancePython -m esptool --chip esp32c3 --port $acceptancePort flash-id
if ($LASTEXITCODE -ne 0) { throw 'Chip/flash check failed' }
```

Confirm C3 with 4 MB flash, then preserve a complete backup outside the repo:

```powershell
$acceptanceBackup = Join-Path $env:USERPROFILE ('esp/hardware-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $acceptanceBackup | Out-Null
$acceptanceImage = Join-Path $acceptanceBackup 'board-before.bin'
& $acceptancePython -m esptool --chip esp32c3 --port $acceptancePort read-flash 0x0 0x400000 $acceptanceImage
if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $acceptanceImage).Length -ne 4194304) { throw 'Backup failed' }
Get-FileHash -LiteralPath $acceptanceImage -Algorithm SHA256
Write-Output $acceptanceBackup
```

Review the dedicated `uthome_cfg` partition at 0x110000, size 0x6000, against
that backup. Never use `erase-all` or write only the app over an old partition
table. The checked build must use DIO/80 MHz/4 MB with these generated offsets:

```powershell
Push-Location -LiteralPath (Join-Path $acceptanceBuild 'build')
try {
    $expected = '--flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 uthome_runtime_port.bin'
    $actual = ((Get-Content -Raw -LiteralPath flash_args).Trim() -replace '\s+', ' ')
    if ($actual -ne $expected) { throw 'Unexpected flash offsets/options' }
    & $acceptancePython -m esptool --chip esp32c3 --port $acceptancePort --after watchdog-reset write-flash '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw 'Flash failed' }
} finally {
    Pop-Location
}
```

Release BOOT, reset if needed and reconnect on the current port. The app uses
binary USB traffic; a quiet terminal is normal. Follow the [hardware guide](../../HARDWARE_ACCEPTANCE.md)
to apply/save EMPTY before each wiring change and then the matching profile.

## Capture acceptance results

For the kernel example, retain startup handover, kernel banner, repeated A/B
ticks and a second reset. For the project fixture, expect `[activation-test] PASS`
and `[port-test] PASS`, then alive counters with no FAIL. Repeat after reset.
These qualify their fixture scope, not peripheral electricity, timing or storage
power-loss behavior. Record new board evidence separately; leave build records
unchanged. Store fresh output under ignored `build/`.

## Fixed memory budget

`static-budget.json` declares the current USB-core reservations, including every
linked application module with writable state. Run in the pinned SDK Python:

```powershell
python tools/target/memory_budget.py --build C:/path/to/qualified-build --output C:/path/to/new-memory-record.json
```

The tool verifies the ELF/map hashes, obtains SDK size attribution, rejects new
unbudgeted application state, checks each module and aggregate reservation, and
checks the linker region capacity. SDK/kernel writable data and alignment are
counted once; the kernel's internal pool already includes stacks carved from it.
RAM-resident code is a separate category. A passed fixed-map check never sets
runtime heap, stack high-water or whole-system qualification to true.

The UART boundary extraction transfers eight bytes of the existing 64-byte
UART allowance to `c3ukern` for its two native handles; `c3uart` retains 56 bytes.
All three budget variants keep their aggregate reservations unchanged. Each
object remains independently bounded, and unlisted writable objects still fail.

Unassigned linker space is not measured free heap or stack headroom. Keep
per-module limits and actual sizes in the generated budget record. Combined
wireless/transaction reservations and measured stacks are required before release.

The optional fixed-queue/event/startup variant uses `--static-net-queues` and
`--budget tools/target/network-budget.json` with the memory checker. Its overall
278,528-byte reservation remains unchanged. The selected limits are 168,192
bytes of application fixed data, 53,184 bytes of SDK/kernel fixed data and
57,152 bytes of RAM code. The checker accepts passed configuration-application
builds and passed isolated Wi-Fi/BLE diagnostics; the resulting record retains
`whole_system_static_qualified=false`. The older `netqueue-budget.json` describes
the preceding queue-only experiment, not the current selected variant. The
scan-only `--wifi-probe` additionally requires that queue option and the isolated runtime
fixture (without `--configuration-app`). Its explicit noncompliant scope and
memory observations are described in the [network guide](../../firmware/src/network/README.md).
Do not install either while another client owns serial. Diagnostic binaries
are not release candidates and must not be substituted for application acceptance.

The separate `wifi-station-budget.json` applies only to a passed, non-diagnostic
Wi-Fi configuration application with static network queues and the static task
pool. It leaves `network-budget.json` unchanged. The station policy attributes
the project-owned `uthome_task` archive once to `task_pool_fixed`; it is neither
application data nor external SDK/kernel data. Its group limits are 147,456
bytes application fixed, 18,560 bytes task-pool fixed, 55,296 bytes external
SDK/kernel fixed and 57,152 bytes RAM code, under the unchanged 278,528-byte
total cap. Every reserved module must be linked, and every linked owned module
must have and remain within its reservation. Run it only against immutable build
evidence, writing a new result outside that evidence directory.

The station-08 map is the policy basis. Existing reviewed per-object limits are
retained where the station links the same module. `c3app` receives one additional
256-byte block above its 42,240-byte measurement; new station objects round
`c3https` to 3,200 bytes (128-byte boundary), `c3net` to 96 bytes and `c3wsta`
to 64 bytes (16-byte boundaries). `c3task` retains its reviewed 18,504-byte
reservation, 35 bytes above the station measurement. Exact-size static pools
remain exact so any capacity change requires review. BLE-only `c3ble` and
`c3gatt` are excluded because the policy requires Wi-Fi-only evidence and neither
object is linked in the reference map.

```sh
python tools/target/memory_budget.py \
  --build /tmp/uthome-wifi-station-08 \
  --output /tmp/uthome-wifi-station-budget-check.json \
  --budget tools/target/wifi-station-budget.json
```

`--ble-probe` selects isolated nonconnectable advertising. Adding
`--ble-gatt-probe` selects the bounded, one-connection, read-only GATT diagnostic
described in the network guide. The qualifier checks actual SDK peripheral-role,
connection, persistence and linked-service settings; neither radio mode can be
combined with the application or model diagnostic. Both retain visible SDK heap
usage and do not establish no-malloc compliance.

The optional `--ble-static-host` modifier also requires the isolated BLE mode.
It selects the SDK's static NimBLE host-global definitions and verifies the
effective setting. It is a footprint experiment, not an exemption or proof that
controller, NPL, task/timer and traffic paths no longer allocate. Use a new build
folder and the same network memory budget for a meaningful comparison.

## Network observations and flash-size analysis

`python -m tools.target.network_acceptance --help` lists finite read-only HTTPS,
MQTT and optional BLE capture commands. They require explicit endpoints and
never flash, read USB, apply configuration or publish MQTT. Reports cannot be
overwritten; a capture is not whole-system qualification. The implementation
checks use offline socket/BLE doubles only.

`python -m tools.target.network_size --help` describes read-only flash attribution
from existing build artifacts; failed builds stay failed. Exact specifications,
commands, resource requests and acceptance matrix are maintained in the
[network guide](../../firmware/src/network/README.md#usb-wi-fi-troubleshooting).

## Authenticated HTTP configuration client

`python -m tools.target.network_config --help` describes the separate mutating
client. `network_acceptance` remains read-only. Supply a trusted CA and set
UTHOME_TOKEN privately in the environment; do not put credentials in shell history.
These are client examples for a later authorized bench session, not build steps:

```powershell
python -m tools.target.network_config https://DEVICE:8443 --ca CA.pem --report state-01.json state
python -m tools.target.network_config https://DEVICE:8443 --ca CA.pem --report apply-01.json apply --request-id 11 --boot-id 0123456789abcdef --request-token operation-11 --revision 8 --expected-revision 7 --profile node.json
python -m tools.target.network_config https://DEVICE:8443 --ca CA.pem --report result-01.json result --request-id 11 --boot-id 0123456789abcdef --request-token operation-11
```

Replace every example ID/revision/boot with freshly observed values. Coordinate
IDs with USB clients. The tool neither allocates IDs nor retries mutations. It
reserves a new report before contact, validates bounded typed replies, matches
boot/ID and checks saved revision/CRC/flags. Result GET describes completion,
possibly historical; query state separately. A lost reply, 404, cache eviction or
uncertain save does not authorize a fresh Apply. Profile bytes are sent exactly;
the node performs full schema validation. No flashing, serial access, redirects,
credential provisioning or MQTT publication occurs. Full transport/error/resource
specifications are in the maintained network guide.

Reproduce the new routing/client tests with:
`python -m unittest tests.target.test_network_config tests.target.test_network_config_client`.
The ESP transport tests are `tests.target.test_network_esp`; use the project's
pinned compiler/formatter environment. All automated HTTP mutation tests use
local doubles and fake storage, never a live device.

Configuration-client reports now include an explicit `outcome` in addition to
controller fields. Interpret it as follows; the client never retries automatically:

| Outcome | Operator action |
|---|---|
| saved_at_completion | Save was confirmed at that completion; if historical, query state before assuming it remains current |
| active_unsaved | Runtime may have changed; persistence is not confirmed. Query state and follow existing USB recovery guidance before another mutation |
| rejected | Review the original full profile and validation constraints; do not resend unchanged input |
| stale / conflict | Re-read state, coordinate USB IDs/revisions, then explicitly decide the next operation |
| recovery_required / storage_error | Inspect state and use the existing controller recovery procedure; do not blindly repeat writes |
| not_confirmed | Delivery or admission is unknown. Query matching result then state; missing/evicted/rebooted results do not authorize replay |
| state_observed | Read-only observation, not proof that an earlier Apply succeeded |

Retain the operation's boot ID, request ID, token and exact original profile bytes
outside the device until resolved. Historical completion is never upgraded to
current state. The network guide's acceptance checklist separates offline test
coverage from board observations and Member 1's resource/timing approval.
