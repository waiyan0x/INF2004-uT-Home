# Target build and qualification tools

`qualify_kernel.py` builds an isolated archive of the pinned kernel with reviewed
patches. It checks clean kernel/SDK revisions and SDK submodules, compiler/tools,
source hashes and compiler profiles. The dependency checkouts stay unchanged.
No build command opens serial, resets or flashes the board.

## Build modes

Activate the pinned [firmware environment](../../firmware/README.md#pinned-target),
then run `python tools/target/qualify_kernel.py inspect` from the repository root.
Use a fresh, nonexistent output directory without spaces. Existing destinations
and paths inside dependencies are rejected, never cleaned. Custom `--sdk` and
`--kernel` copies must match the pins.

| Flags | Image purpose |
|---|---|
| Neither mode flag | Upstream `examples/tasks` two-task kernel example |
| `--runtime-port` | Project acceptance fixture; no peripheral activation |
| `--runtime-port --configuration-app` | USB/profile/storage application used with the UI and peripherals |

## Configuration application build

```powershell
python tools/target/qualify_kernel.py build --runtime-port --configuration-app --work-dir "$env:USERPROFILE/esp/uthome-config-new-build" --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

The compatibility patch supplies missing ESP-IDF 6.1 systimer dependencies; omit
it only to reproduce the known upstream build failure. The resource patch adds
512 usable native UART bytes and disables unused ADC startup. Patches are applied
only to the staged copy and hashed. The app has a 64 KiB kernel-heap override;
the fixture uses 32 KiB. Neither is a free-RAM measurement.

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

## Identify or recover the board

Disconnect peripherals and other serial clients. Use a known USB data cable,
then `python -m serial.tools.list_ports -v`; Bluetooth ports are not the board.
Replace COMX in `python -m esptool --port COMX flash-id` with the actual port.
Confirm ESP32-C3 / 4 MB. A power LED alone does not prove USB data works.

If absent, try a direct USB port/data cable; hold BOOT, press/release RST, then
release BOOT after a second for ROM download mode. Keep GPIO9/BOOT and native
USB18/19 free. A board stuck in download mode after flashing may need BOOT
released and RESET, or esptool's watchdog reset. See the [recorded behavior](../../firmware/qualification/README.md).

## Install the configuration application

This procedure **writes firmware**. Start with the board alone and serial clients
closed. Use a newly built, verified configuration application, not the fixture
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
binary USB traffic; a quiet terminal is normal. Follow the [hardware guide](../../CONTRIBUTING.md)
to apply/save EMPTY before each wiring change and then the matching profile.

## Capture acceptance results

For the kernel example, retain startup handover, kernel banner, repeated A/B
ticks and a second reset. For the project fixture, expect `[activation-test] PASS`
and `[port-test] PASS`, then alive counters with no FAIL. Repeat after reset.
These qualify their fixture scope, not peripheral electricity, timing or storage
power-loss behavior. Record new board evidence separately; leave build records
unchanged. See the [evidence index](../../firmware/qualification/README.md).
