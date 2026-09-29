# USB configuration and persistent storage: checkpoint 5c

The configuration application now connects `cfgpump` -> `cfgctrl` ->
`cfgbind`/`activate` and `cfgstore` -> `c3store`. `c3usb` is the exclusive native
USB endpoint. All large contexts are static and all controller calls use one
kernel task. Runtime, framing and persistence contain no prototype device names.
The binder remains the explicit compiled-driver boundary documented in
[ADMISSION.md](ADMISSION.md).

This is software and build verification. No board was flashed, reset or opened
over serial. Hardware acceptance remains checkpoint 8; the conservative
`configs/builds/target-pending.json` manifest stays empty until that acceptance.
The original 5c verification below predates
[telemetry/health](../services/TELEMETRY.md) and [OLED binding](../services/DISPLAY.md),
now implemented at 6a/6b. Capabilities reporting, the local UI and PWM/rule routing
remain later checkpoints.

## Application and build modes

The existing approved `usermain` entry point selects the acceptance fixture by
default. `--runtime-port --configuration-app` selects `c3app_run`, after starting
the existing idle task. This reuses the upstream 8 KiB initial-task stack and
moves its priority to 20 so the admitted device workers can preempt it.

Application startup initializes bounded JSON hooks and the activation owner,
selects storage, takes exclusive USB ownership, attempts restore once, then
polls one transport step and yields for 1 ms. Restore revalidates saved JSON
against this build before activation. Missing/unreadable storage stays visible
as STORAGE; malformed records leave a safe empty configuration. Opening storage
does not erase, format or migrate it. A missing partition needs the correct
image/layout and a later reboot; the adapter does not invent a fallback.

From an activated pinned ESP-IDF environment, build only:

```powershell
python tools/target/qualify_kernel.py build --runtime-port --configuration-app `
  --work-dir "$env:USERPROFILE/esp/uthome-config-new-build" `
  --compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch `
  --resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch
```

Use a fresh output directory. Omit `--configuration-app` for the no-peripheral-I/O
acceptance image. Both modes are compiled with their actual linked dependencies;
the qualifier records the mode, source hashes and generated compiler options.
The configuration application adds its own partition defaults while retaining
the pinned kernel defaults unchanged. No new kernel patch or C exception is used. Application CMake rejects a
configuration with routine SDK logs enabled or the wrong console selected.

## Transport and reconnect contract

`cfgpump` processes at most 64 received bytes, one controller request and one
48-byte reply at a time. It retains input following a complete frame and resumes
partial writes. Empty polls still expire incomplete frames. A whole reply has a
2-second budget starting on the next pump step after controller work returns.
Backpressure, disconnect or invalid callback counts discard transport state;
they do not roll back an applied configuration or clear the controller retry
cache. Full frame admission remains bounded to 8192 bytes and 2 seconds from
the first magic byte consumed by the parser. This is a polled stream, not an
arrival timestamp for bytes still in USB hardware or queued behind a reply.

The C3 backend uses the pinned SDK's USB FIFO LL interface, without installing
a FreeRTOS driver, remuxing USB pins or waiting for a host. Reads are <=64 bytes;
writes are <=48 and explicitly flush short packets, including binary replies
without a newline. It refuses an already interrupt-owned endpoint. The
application build disables normal ESP-IDF logs and must be the only USB reader/writer
after the boot banner. Future services must use framed traffic, never console
prints. Panic/boot output can interrupt a frame; the host scanner rejects it and
resynchronizes. No sleep/USB power-management support is claimed.

Version 1 gains command **PROBE=5**, revision zero, empty payload, nonzero
correlation ID. Its 48-byte response retains the existing six uint32 fields,
except field 0 is **last_request_id**, not a status. The other fields are active
revision, saved revision, active CRC, saved CRC and flags. A probe performs no
storage access, consumes no sequence ID and never evicts the retry cache.
Saved state is the last observation: flags must be inspected. QUERY with
`last_request_id + 1` refreshes durability. At UINT32_MAX only PROBE is available
until reboot; never wrap IDs. Older firmware silently rejects PROBE and the
new client times out without sending a mutation.

Only one host client may control the port. The simple Python client first probes
and queries, then sends an explicitly requested mutation with the next ID.
It handles partial reads/writes, boot noise and unrelated/corrupt responses.
Its 10-second operation deadline requires finite underlying serial timeouts.
It does not automatically resend a mutation after any ambiguous failure.
Reconnect and QUERY active/saved revisions and CRCs before deciding whether to
resend or SAVE. If an exact retry is deliberately issued during the same boot,
the controller caches the last identical request; probing cannot evict it.
QUERY does consume an ID and replaces that cache. There is no exactly-once
guarantee across power loss/reboot and no authentication on this local USB link.

## Flash layout and durability

The application preserves the default NVS, PHY and 1 MiB factory-image ranges.
It adds custom type/subtype `0x40/0`, label `uthome_cfg`, at **0x110000**, size
**0x6000**. Each slot is three 4096-byte erase sectors (12288 bytes), enough for
the 32-byte header and 8192-byte payload. The adapter checks internal flash,
exact address/size/erase geometry, writable and unencrypted flags before use.
It checks slot/offset/length with subtraction before addition, then calls the
bounded `esp_partition` APIs. It never accesses NVS, another slot or raw flash.

SDK read/write/erase calls are synchronous; the adapter has no pending write
buffer, so sync validates ownership/availability. `cfgstore` still reads back
headers/payload, writes its commit marker last and reads back the committed
record. An error can occur after a durable write: active/saved state remains
separate and QUERY resolves uncertainty. There is no wear levelling; save only
on explicit accepted changes/retries, never from the polling loop. Physical
power-loss behavior, endurance and flash latency still need board acceptance.

The upstream shim suspends kernel dispatch around cache-disabled flash work.
Flash erase/write can delay device workers and non-IRAM interrupts. Do not infer
deadline compliance from the build or claim that the 1 ms polling delay bounds
activation, flash access or an entire request. Combined sensor/flash load and
missed pulse/UART behavior must be measured in checkpoint 8.

Installing this application later requires reviewing the partition-table change
against the saved board backup. Replacing only the application on the old table
leaves STORAGE faults. Updating a table does not erase old bytes; do not issue a
blanket erase or assume a profile area is empty. This checkpoint only builds it.

## Manual BARR-C review

- Modules use paired headers, explicit named callback types and C99 syntax;
  the existing entry/test retains its already approved C11 scope. No formatter,
  policy, approval or exception record is changed.
- Sizes, CRCs, offsets and wire fields use explicit widths. Narrowing at USB
  calls is bounded to 64/48 bytes; signed counts are checked before conversion.
  Custom partition enum casts are SDK-documented 8-bit values. No packet or
  flash record is accessed through an unaligned native-structure cast.
- Module globals remain private. Static controller/binder/storage/pump contexts
  have application lifetime; callback dependencies and the SDK partition object
  stay borrowed. Single-owner calls and reentry checks preserve their lifetime.
- USB MMIO remains inside the SDK LL interface; no owned register casts or new
  ISR/task entry ABI is introduced. Blocking flash work is explicit. USB loops
  have fixed limits and no task/heap allocation. All meaningful cleanup/error
  results are retained; intentional ignored pump/restore errors have comments
  because the endpoint must continue and QUERY exposes controller state.
- Host tests cover interrupted saves, exact retries, probe cache preservation,
  coalesced/fragmented input, partial writes, idle expiry, stalled TX, disconnect,
  invalid counts, reentry, Python/C parity and ambiguous host outcomes. The board
  fixture adds six unopened-adapter checks, for 568 expected checks; these have
  not been run on hardware. Full source/hash/build evidence is recorded separately.

SDK references: [USB console behavior](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-guides/usb-serial-jtag-console.html),
[partition layout](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c3/api-guides/partition-tables.html).
Exact compatibility was checked against the locally pinned SDK source, including
`esp_hal_usb/esp32c3/include/hal/usb_serial_jtag_ll.h`, `esp_partition.h` and the
pinned kernel's flash/dispatch shim. These references do not replace board tests.

## Measured software/build result

All **134 host tests pass** (54 standard, 26 foundation, 18 runtime, 36 target).
The mandatory gate covers **166 C/H files** with zero findings/tool-policy errors.
Both real target modes pass with 110 source inputs and 45 checked owned compiler
profiles. Four new modules use C99. The existing C11/GNU99 boundaries, pinned
dependencies, approval and deviation records remain unchanged.

The application ELF retains these contexts:

| Context | Static bytes |
|---|---:|
| Binder | 5,944 |
| Controller | 16,464 |
| Storage scratch/state | 8,208 |
| Transport pump, including wire receiver | 8,408 |
| **Total of these four** | **39,024** |

Do not add the included wire receiver again. The parser may separately request
up to 32768 bytes including its wrapper headers, plus allocator bookkeeping.
Activation plans, queues, other global state and all task stacks are additional.
These link-time sizes do not establish a future AI memory reserve; measure free
heap and stack high-water marks under combined load in checkpoint 8.
See [fresh evidence](../../qualification/esp32c3-usbstore-20260928.json).

## Checkpoint 6a update

The owner loop now services heartbeat/health work and exposes command 6 TELEMETRY
through a read-only pump callback. Its reply is variable length; commands 1..5
retain their 48-byte replies. No storage access, request-ID consumption or retry
cache eviction occurs for telemetry. See [the current protocol](../services/TELEMETRY.md).
The enlarged pump and new static observation contexts supersede the older memory
table above; fresh sizes are recorded with checkpoint 6a evidence. Display, local
UI, capability presentation and rules remain pending.
