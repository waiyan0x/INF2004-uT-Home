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

Wi-Fi service builds also stage `network-power.defaults` from the private
network settings. Qualification checks both this generated input and the
compiled `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER`, and records
`wifi_startup_power_dbm` in build evidence. See the
[network setup guide](../../firmware/src/network/README.md#usb-wi-fi-troubleshooting)
for startup/runtime power units and limits. The generated header and firmware
contain credentials and must remain outside Git.

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
`c3net` to 96 bytes and `c3wsta`
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

`python -m tools.target.network_acceptance --help` lists finite read-only MQTT
and optional BLE capture commands. They require explicit endpoints and
never flash, read USB, apply configuration or publish MQTT. Reports cannot be
overwritten; a capture is not whole-system qualification. The implementation
checks use offline socket/BLE doubles only.

`python -m tools.target.network_size --help` describes read-only flash attribution
from existing build artifacts; failed builds stay failed. Exact specifications,
commands, resource requests and acceptance matrix are maintained in the
[network guide](../../firmware/src/network/README.md#usb-wi-fi-troubleshooting).

## TLS connect retry probe

`tls_connect_probe.py` executes the compiled `c3mqtt` connect callback and the
pinned ESP-TLS connect path in RV32 instruction emulation. It uses the existing
optional `unicorn==2.1.4` and `pyelftools` dependencies. Run from the root with
an ESP configuration-application ELF that contains DWARF information.

```powershell
python -B -m tools.target.tls_connect_probe ORIGINAL_ELF --expect stalled --output build/tls-retry/original.json
python -B -m tools.target.tls_connect_probe CORRECTED_ELF --expect recovered --output build/tls-retry/corrected.json
```

Create the report directory first. Existing reports and disabled Python
assertions are rejected. Each image runs twelve cases across the raw SDK and
owned adapter, with zero, one or three initial readiness timeouts and either
read or write readiness afterward. The raw SDK control keeps reproducing the
cleared-set defect. The original adapter also stalls, and the corrected adapter
reaches the handshake once readiness arrives. Continued empty-set polls are
checked explicitly. The probe starts at the established SDK connecting state
and reads context/configuration offsets and global locations from target DWARF.
It requires the selected image's eight-byte VFS descriptor sets.

Socket readiness, socket-error reads, TLS context creation, the handshake and
ROM helpers are doubled. The report records these boundaries, the exact ELF
hash and every poll/result. This check establishes connect-state progression.
Radio, encrypted handshakes, board timing and physical recovery need live
acceptance. The [network guide](../../firmware/src/network/README.md#mqtt-tls-connect-retry)
records the completed before/after check and the fresh target build.

## TLS fragment reproducer

`tls_fragment_probe.py` reproduces the pinned SDK's optional dynamic-buffer
partial-header failure by executing RV32 instructions in software. It needs a
dynamic-buffer ELF with DWARF and the optional host packages `unicorn==2.1.4`
and `pyelftools==0.33`. These are probe dependencies, not firmware dependencies
or requirements for the ordinary host suite. A successful exit means all four
split cases reproduced the defect, with an unsplit control case passing.

The current application deliberately rejects this mode. To recreate only the
isolated experiment, start from its recorded pre-guard revision:

```powershell
git worktree add --detach build/tls-buffer-probe 23f1f267d6afeff25c53ce370c8fd78c17ccd5c2
Add-Content build/tls-buffer-probe/tests/target/runtime_port_board/network.defaults "`nCONFIG_MBEDTLS_DYNAMIC_BUFFER=y"
```

From that worktree, use the normal pinned build procedure with
`--runtime-port --configuration-app --static-net-queues --network-services wifi`,
build-only settings and a fresh output directory. Supply the normal pinned
kernel checkout explicitly; the worktree does not initialize another submodule.
Keep this rejected experiment separate from working firmware and do not flash it.
Back in the maintained checkout, install the optional packages in a local virtual
environment, then run:

```powershell
python tools/target/tls_fragment_probe.py ABSOLUTE_PROBE_WORK_DIR/build/uthome_runtime_port.elf --output build/tls-capacity/new-fragment-report.json
```

Create the output directory first; existing reports are not overwritten. The
tool maps the ELF into emulator memory without changing the file. Only transport,
allocator and ROM byte-copy routines are simulated; the SDK receive helper and
input-fetch routine execute their compiled instructions. Between retry calls,
the returned stack is overwritten to represent valid intervening work. This
does not emulate radio hardware, complete a TLS handshake or measure ESP timing.
The [resource assessment](../../docs/reports/wifi-static-memory/README.md#dynamic-tls-buffer-assessment)
records the source/build identity, result and remaining memory constraints.

## TLS output allocation probe

`tls_output_probe.py` checks the compiled `c3tout` adapter with the actual pinned
SDK setup/free instructions. Use the same optional `unicorn==2.1.4` and
`pyelftools` environment as the fragment reproducer, and a qualified Wi-Fi/static
queue image built with the current 16,384/4,096-byte TLS content limits:

```powershell
python -B -m tools.target.tls_output_probe ABSOLUTE_WORK_DIR/build/uthome_runtime_port.elf --output build/tls-output/new-sdk-report.json
```

Run from the repository root with assertions enabled. Create the output folder
first; reports are never overwritten. The tool uses ELF/DWARF offsets and runs
the real SDK setup, buffer-pointer initialization and cleanup code. It doubles
the heap, ROM byte-copy functions and PSA hash setup/abort; it does not perform
a TLS handshake, network I/O or hardware timing. The MQTT context must receive
its aligned input/output pair, a second must fail without heap fallback,
release must erase and reuse both slots, and any remaining setup heap failures
must leave no simulated heap objects. Current setup state is fixed; the state
probe below injects exhaustion for those records. The report
records the ELF hash, DWARF/linked buffer capacities and original SDK execution
counts. The ELF is not modified.

## TLS handshake-state lifetime probe

`tls_state_probe.py` exercises compiled SDK lifecycle instructions using the
actual fixed typed stores. Use the existing optional Unicorn/pyelftools
environment, the qualified Wi-Fi ELF and its matching passing memory report:

```powershell
python -B -m tools.target.tls_state_probe ABSOLUTE_WORK_DIR/build/uthome_runtime_port.elf --memory-report build/tls-state-fixed-20261117/memory-final.json --output build/tls-state/new-sdk-report.json
```

Create the output directory first. Assertions must remain enabled; existing
reports and mismatched ELF/memory identities reject. SDK types are checked from
DWARF and ELF symbols identify two typed transforms, two sessions, one handshake
record and their leases. Both owned renegotiation wrappers run 100 cycles plus
100 in-progress retries each through native SDK code. Tests check separate
old/new pointers, retained active bytes, promotion, full-record erasure and
cleanup with zero state-record heap requests. Eight native reinitialization
cases retain existing records and replace only missing ones. Occupied-slot
injection tests each state kind through setup and both renegotiation entries:
nine failures must preserve active/reserved storage without heap fallback.
Selection must end before hash setup or handshake progression; equal-sized
requests outside initialization still reach the SDK allocator. Old heap-state
images and either linked session-reset API reject.

The linked stores and lease code are real; heap, ROM byte operations and PSA
hash calls are doubled. Missing-record injection releases empty outer records;
`mbedtls_ssl_handshake` returns WANT_READ through a double. Empty crypto state is
advanced directly to native wrapup. No authentication, encrypted handshake,
cipher handover, radio or runtime peak is tested. The current ELF has no linked
session-reset API, and the probe does not claim reset coverage. Memory headroom
comes from the matching linked-image report, including scope/lease metadata;
runtime peaks and remaining certificates, keys, tickets and TCP/IP allocations
are unqualified. See the [current assessment](../../docs/reports/wifi-static-memory/README.md#fixed-tls-handshake-state).

## TLS retained-hostname probe

`tls_name_probe.py` checks the compiled fixed-name wrapper and the actual SDK
setter, setup and cleanup. Use the same optional Unicorn/pyelftools environment
and a current Wi-Fi/static-queue ELF:

```powershell
python -B -m tools.target.tls_name_probe ABSOLUTE_WORK_DIR/build/uthome_runtime_port.elf --output build/tls-name/new-sdk-report.json
```

Create the output directory first; reports are not overwritten and assertions
must remain enabled. The probe binds the 254-byte slot to its ELF/DWARF identity.
It compares copied names and context flags with the unwrapped SDK setter,
including maximum/short/empty names and NULL clearing. The unwrapped reference
must allocate, while wrapped calls must not. It also checks overlength rejection
without replacing the old name, second-context exhaustion, reuse, each of the
three occupied-state setup failures, unrelated allocation forwarding and
100 complete setup/free cycles. The report records the ELF hash and SDK counts.

Heap, ROM byte/string functions (including `strlen`) and PSA hash operations are
doubled. The pinned SDK uses PSA errors -135 for invalid input and -141 for
insufficient memory. This checks name storage and native setter semantics;
it does not test certificate authentication, a handshake, radio or timing.

## TLS certificate-record probe

`tls_certificate_probe.py` runs native certificate framing, identity and ownership
instructions with the actual linked stores. Use the existing optional
Unicorn/pyelftools environment with a DWARF-enabled Wi-Fi ELF:

```powershell
python -B -m tools.target.tls_certificate_probe ABSOLUTE_WORK_DIR/build/uthome_runtime_port.elf --expect fixed --output build/tls-certificate/new-sdk-report.json
```

Create the output directory first; disabled assertions and existing reports
reject. `--expect heap` calls the unwrapped native parser as an allocation-positive
reference, including on the fixed image. It must issue 202 outer allocations
across initial parsing and 100 renegotiations; the fixed wrapper must issue none.
DWARF and symbols bind the 952-byte native chain head and 32-byte digest to their
leases. Both stores must erase and be reusable after native cleanup.

Seven error cases cover a recovered read retry, malformed framing, a changed
certificate, DER error, verification error, replacement-hash error and key-import
error. The probe distinguishes preserved old digests from the SDK's deliberate
clear-after-match behavior. Two occupied-store failures check deterministic
exhaustion without fallback. The unwrapped reference injects the corresponding
heap allocation failures. Both paths must retain the same native outcomes.

It uses one synthetic certificate. Record reading, DER parsing, trust validation,
SHA-256 calculation, public-key import, alerts, heap, ROM helpers and hash/handshake
progress are doubled. DER internals, multi-certificate chains, authentication,
encrypted handshake, runtime peaks and hardware remain untested. See the
[current record assessment](../../docs/reports/wifi-static-memory/README.md#fixed-peer-certificate-records).

## TLS input lifetime and capacity probe

`tls_input_probe.py` runs the existing SDK's receive path from a qualified,
non-dynamic-buffer ELF. Use the same optional probe dependencies above. Pass a
successful memory-budget report for that exact ELF; mismatched hashes reject.

```powershell
python -B -m tools.target.tls_input_probe ABSOLUTE_WORK_DIR/build/uthome_runtime_port.elf --memory-report build/tls-input-fixed-20261117/memory.json --output build/tls-input/new-sdk-report.json
```

Create the output folder first. Assertions must be enabled and reports are never
overwritten. One MQTT session exercises four partial-header cases, three
partial-body cases with 16 KiB content, one oversized fetch, one partial EOF
case and three plaintext reads. Consumed plaintext must be erased; unread bytes
remain attached to the connection. All session allocations must be released.

The tool reuses the output probe's ELF loader, SDK setup and cleanup execution.
Heap, transport, ROM byte copies and PSA hash operations are doubled. The read
cases inject already-decrypted data; they do not test authentication, decryption,
a handshake or radio hardware. No firmware or SDK source is changed. A passing
result checks fixed receive allocation and lifetime in the compiled image;
remaining TLS allocations and physical acceptance are separate.

The report verifies that the SDK uses the adapter's actual ELF/DWARF input array
and issues no input-buffer heap request. It reports linked capacity and remaining
application/total headroom from the matching memory report. This is no longer a
projection; runtime free heap and remaining crypto/TCP/IP needs are unmeasured. See the
[capacity decision](../../docs/reports/wifi-static-memory/README.md#tls-input-lifetime-and-capacity-decision)
before attempting buffer reuse or changing record limits.

## TLS record compatibility probe

`tls_profile_probe.py` builds the pinned SDK's mbedTLS fork as host libraries
and uses real TLS 1.2 encryption over loopback TCP. It compares input/output
content limits of 16384/4096, 4096/2048 and 2048/1024 bytes. Dependencies are
the existing `tools/transaction/requirements-dev.txt`, CMake, Ninja and GCC.
Use the pinned SDK checkout and a **new** directory below root `build/`:

```powershell
python -B -m tools.target.tls_profile_probe --sdk C:/Users/kkak4/esp/uthome-c3-sdk-v6.1 --cc C:/Users/kkak4/mingw64/bin/gcc.exe --output build/tls-profile/new-run
```

The command checks clean SDK and nested mbedTLS revisions, builds with warnings
as errors and assertions enabled, and retains compiler commands, library/source
hashes and a JSON report. It refuses disabled Python assertions and existing
output directories. Fresh one-day test keys, certificates and broker credentials
stay in the ignored output directory. It never opens serial or installs firmware.

Four cases run per profile: authenticated MQTT with a 96-byte round trip;
wrong trust root; wrong server name; and a longer valid broker certificate chain.
The report records negotiated protocol, cipher, verification flags, received
record sizes and exact errors. Expected compatibility failures remain failures.
The SDK aliases SSL bad-input/buffer-too-small results to PSA -135/-138; the
Python binding labels those explicitly because the legacy SDK error formatter
otherwise produces misleading text.

`tls_profile_peer.py` drives the serialized C peer in
`tests/target/tls_profile/`. Its two 32 KiB ciphertext queues are **host transport
storage**, not proposed ESP reservations. TLS buffers, certificate parsing,
authentication, encryption and cleanup execute in the unmodified pinned SDK.
The SDK uses its native host configuration/software crypto and heap, restricted
at runtime to TLS 1.2 and ECDHE-ECDSA/RSA with AES-128-GCM/SHA-256. These suites
are enabled in the selected ESP build; the host build is not the ESP ABI or its
complete compile-time feature set. The empty host `sdkconfig.h` supplies the
include expected by the SDK bignum/ecp wrappers without enabling hardware paths.

MQTT uses the existing authenticated AMQTT fixture and a test packet encoder,
not `c3mqtt`. One C connection runs at a time. No no-heap claim, ESP timing,
radio compatibility, arbitrary certificate compatibility or hardware acceptance
follows from this experiment. The removed HTTPS upload cases remain only in
earlier immutable reports. See the [compatibility decision](../../docs/reports/wifi-static-memory/README.md#tls-record-compatibility-decision).

### Host TLS fixture C review

The 17 November 2026 review covers `tlspeer.c/.h` and the empty host SDK header.
They follow the maintained templates and ordinary C99, with matching public
prefixes and no new exception. SDK callbacks retain their declared types without
function-pointer casts. A single owner serializes all calls; start/close own the
client context/config/CA lifetime. File paths and names are used only
during start; the SDK copies/parses their contents. Read/write buffers remain
caller-owned, and a WANT_READ/WRITE retry retains the same write bytes/length.
The configured cipher list has static lifetime. Returned protocol/cipher strings
are read before close. The fixture closes every case, including rejected trust
and handshake failures.

Queue addition checks remaining capacity before copying; subtraction follows
the smaller of capacity/available bytes. Overlapping compaction uses `memmove`.
The two `size_t`-to-`int` callback casts are bounded by the 32768-byte queue and
the host compiler's `INT_MAX`; send also asserts the conversion bound. External
status codes remain signed. State is initialized before use and released after
every case. Owned C has no loops, task/ISR, MMIO, assembly or allocator calls;
SDK heap/entropy/crypto are intentionally real host implementations. The single
client verifies server trust/name; no verification bypass is added. Current
checks and fresh target evidence are in the [network guide](../../firmware/src/network/README.md#esp-https-removal).
This remains manual/host verification, separate from physical acceptance.
