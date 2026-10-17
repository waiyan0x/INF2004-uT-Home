# Device observations (D2/D4)

This path collects evidence on a reviewed connection. Identity is resolved by
the PC catalogue, without a trained model. The generic runtime and node schema
remain unchanged. Hardware acceptance of this new path is still pending.

## Small, separate responsibilities

- `disci2c.c`: bounded register reads through the existing I2C bus API. It knows
  addresses, registers and returned bytes, not device names or expected IDs.
- `discuart.c`: a bounded, receive-only window through `uartio` and `ldparse`;
  returns report/error counts without claiming an exact model.
- `cfgdisc.c`: compiled connection presets, ownership checks, configuration
  recovery and explicit USB encoding. It does not identify or configure devices.
- `tools/config_transfer/discovery.py`: request encoding and strict reply checks.
- `tools/device_discovery.py`: deterministic matching against
  `catalogue/discovery.json` and existing descriptors; checks advertised drivers.

A new identity using an already collected register can be added through reviewed
catalogue data. A new address, register, wiring preset or observation method
requires a reviewed firmware change and build. This is not a universal scan or
an arbitrary register-command interface.

## Current connection and ownership

`catalogue/connections/bench-i2c.json` describes preset `bench_i2c` version
`0.2.0`, wire code 1: I2C0, SDA GPIO0, SCL GPIO1, 100 kHz, reviewed 3.3 V supply
and logic. The compiled table reads register 0x00 at 0x76 and 0x77. Each operation
writes only the register pointer, then reads one byte with a repeated start.
Even a register-pointer write needs review for the equipment on that bus.

The client requires explicit wiring confirmation before any serial operation.
Rewire with power off; this is not electrical hot-plug support. No probe runs
automatically at startup or saved-profile restoration.

The serialized configuration owner accepts discovery only with no active
workers, no retained bus handles and an empty hardware configuration. Both
`devices` and `interfaces` must be empty, including disabled instances. This is
intentionally stricter than checking one bus: the current C3 backend requires
workers to be stopped before opening it. A busy request performs no bus I/O and
does not stop an existing setup. Use the normal reviewed APPLY path to select an
empty hardware profile if needed; RECOVER is only accepted in recovery state.

The core allows up to eight unique, nonreserved addresses. This preset uses two.
Each transfer has at most 100 ms within a 200 ms acquisition/read budget.
Mandatory bounded cleanup follows even after that budget expires; reported
elapsed time includes cleanup. Backend callbacks must be bounded, serialized,
non-reentrant and use the same monotonic clock. A NACK permits the next approved
read; a timeout or bus error stops the attempt and leaves later records
`not_checked`. Late/failed reads never publish a byte as valid evidence.

Every acquired handle is closed, including handles returned alongside an open
failure. A failed close retains ownership, reports a fault and puts the
configuration controller into recovery. APPLY/SAVE remain blocked; the existing
explicit RECOVER command retries discovery cleanup before binding recovery.
USB disconnect does not interrupt the synchronous probe: its deadline and
cleanup still run. There is no cancellation command or automatic retry.

## USB contract

Feature bit 1 (value 2) in CAPABILITIES advertises I2C discovery. Bit 2 (value 4)
advertises UART discovery. Bit 0 retains its existing rule-routing meaning. DISCOVER is command 8, reply 0x88, using the
existing 24-byte UTH1 frame and CRC. Header revision is the expected active
configuration revision, including zero. The reply echoes it; the payload also
reports the actual active revision. DISCOVER does not consume mutation request
IDs, evict their retry cache, activate a profile or access persistent storage.
The convenience client first queries capabilities and current state; that
ordinary QUERY retains its existing sequencing/storage behavior.

Request payload (24 bytes, little-endian):

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | Format version 1 |
| 4 | u32 | Preset code 1 (I2C) or 2 (UART) |
| 8 | u64 | Nonzero client-session nonce |
| 16 | u32 | Wiring confirmation, exactly 1 |
| 20 | u32 | Reserved, zero |

Reply payload (80-byte header followed by preset-specific records):

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | Format version 1 |
| 4 | u32 | Result: 0 complete, 1 busy, 2 fault, 3 invalid, 4 stale, 5 incomplete |
| 8 | u32 | Echoed preset code |
| 12 | u32 | Actual active configuration revision |
| 16 | u64 | Echoed client-session nonce |
| 24 | u64 | Observation start, board monotonic milliseconds |
| 32 | u32 | I2C elapsed including cleanup; UART elapsed to last clock observation |
| 36 | u32 | Record count: I2C zero or two; UART zero or one |
| 40 | 32 bytes | Actual application's ELF SHA-256 from the SDK descriptor |
| 72 | 8 bytes | Reserved, zero |
| 80 onward (I2C) | 4 u32 per record | Address, register, status, returned byte |

Record status: 0 responding, 1 not responding/NACK, 2 timeout, 3 bus fault,
4 not checked. Returned value is zero unless responding. Records remain ordered
0x76 then 0x77, register 0. No C structure representation crosses the wire.

The nonce identifies a client session, not a device boot and not authentication.
Create a new client after disconnect; discard pending reviews after reconnect,
reboot, rewiring or profile replacement. The image hash identifies a build, not
a boot. Saved result JSON is historical evidence, not proof of current presence.

## Host result and usage

With the new configuration application installed and an idle, reviewed setup:

```powershell
python -B -m tools.config_transfer discover --port COM8 --wiring-confirmed
```

Use the actual port and one exclusive client; close any connected configurator
or serial monitor first. This command neither flashes nor applies/saves a plan.
Exit 0 means the observation completed, including a complete no-response result;
it does not mean a known device was found. Exit 2 is a reported non-complete
attempt; exit 1 is a client/transport error. No automatic retry is attempted.

Only complete, successful observations are matched. The initial catalogue rule
maps 0x50 at register 0x00/address 0x76 or 0x77 to BMP388; another returned value
stays unknown. Multiple matches remain ambiguous. A match can be verified yet
unsupported when the build lacks its driver. Hardware qualification is separate.
These facts do not verify breakout wiring, voltage tolerance or normal operation.

D3/D4 now show these observations and sourced recommendations in the existing
Configuration Desk. Only confirmed, supported identities can produce an ordinary
draft addition, followed by separate validation/review/apply. Manual OLED/GPIO
selection never invokes a firmware probe. See [the UI guide](../../../tools/configurator/README.md#radar-and-manual-devices-d4).

## UART preset and summary (D4)

Preset `bench_uart` 0.1.0, code 2: UART1, RX20/TX21, 256000 baud, 8N1,
512-byte receive buffer, reviewed radar supply 5 V and logic 3.3 V. The existing
native backend configures TX idle but discovery never calls `uartio_write` or
sends payload commands. It exclusively opens the port, flushes an entry snapshot,
listens with bounded 64-byte reads and a one-millisecond owner-task yield, then
closes on every acquired path. No alternate baud rates or pin probing are tried.

The nominal listening window is 1000 ms. Completion may be observed up to 100 ms
late due to scheduling; longer gaps give incomplete results. At most 2000 reads
bound a stopped clock; backwards/failed clocks and receive errors fail closed.
The UART header elapsed field is the last measured time within the listen loop,
not an estimate of open/cleanup time. Opening/flushing/closing use the existing
bounded backend contracts. The one-second request blocks the otherwise idle
configuration owner; no configured worker is stopped to allow it.

The 32-byte UART record contains eight little-endian u32 values:

| Offset within record | Meaning |
|---|---|
| 0 | Parser code 1: existing LD2450 report parser |
| 4 | Received bytes, at most 128000 |
| 8 | Complete reports, including zero-target reports |
| 12 | Parser framing errors |
| 16 | Discarded bytes |
| 20 | Receive errors (zero or one; first error ends the window) |
| 24 | Pending partial-report bytes, less than 30 |
| 28 | Completed window: 1000 ms, or zero if incomplete |

Busy/invalid/stale replies have no record. Faults after entering the observer
retain a diagnostic record and never yield identity. C and Python independently
bound sizes/counts; the host also checks byte-count consistency. Initial noise
or starting partway through a report may conservatively return unknown; retry
explicitly after checking the connection. There is no automatic retry.

Catalogue policy requires a complete window, at least three accepted reports,
no receive/framing errors and no discarded bytes. This yields a protocol
candidate, not verified model identity; the user confirms the actual module.
Silence does not prove removal. A matching parser has no checksum or unique ID,
so confirmation and later physical acceptance remain necessary.

Failed UART close retains ownership and sets the same recovery condition as
I2C. The native backend can remain poisoned after some close failures, requiring
reboot; RECOVER must not claim success in that state. GPIO/manual confirmation
uses no UART resources. No automatic actuator or OLED test is added.

## Verification

`tests/target/disctest.c` runs the production core, service and USB pump against
bounded fake I2C operations. It covers unknown bytes, both approved addresses,
NACK, timeouts, whole-request budget, partial reads, contention, stale revisions,
invalid requests, failed-open cleanup, retained failed-close ownership/recovery,
fragmented transport and unchanged configuration/cache state. The target Python
test decodes a frame actually produced by C. `tests/discovery/` checks malformed
frames, catalogue extension/ambiguity, missing drivers and no retry after loss.

Real C3 compilation is separate from physical acceptance. The later board check
must exercise known/no response, busy configured hardware, reconnect and return
to normal operation. A new build passing does not certify detection on hardware.

The [2026-10-12 evidence](../../qualification/esp32c3-discovery-20261012.json)
records 188 passing tests, the mandatory 186-file BARR-C gate, both successful
C3 builds and their artifact/source hashes. Target/discovery tests were rerun
after the SDK include boundary fix. No board operation was performed.

## Manual BARR-C review

Reviewed 2026-10-12: `disci2c`, `cfgdisc`, `disctest`, and changed `cfgwire`,
`cfgpump`, `cfgbind`, `c3app`, board `main` and `bindtest` integration. This is an implementation
review, not approval of a new deviation or physical hardware qualification.

| Rules | Review result |
|---|---|
| 1.1, 1.5–1.8 | New modules use C99, paired public prefixes and const/static ownership. Casts are bounded byte/word encoding or validated enum/count conversions. Existing C11/GNU99 boundaries are unchanged. |
| 1.3–1.4, 2–3 | Pinned formatting and 80-column policy applied; comments describe deadlines, clock/owner assumptions and failure retention. No dead code or new suppression. |
| 4 | Bus observation, configuration ownership and PC identity remain separate. Public callbacks have named types; internal helpers and tables are static. |
| 5 | Explicit little-endian fields, unsigned shifts, checked sizes/ranges, enum validation and no native structure serialization. |
| 6–7 | Zero-initialized lifetime contexts; bounded loops/functions; serialized owner only, no task/ISR callback ownership changes, no heap in the core. Borrowed buffers live through each synchronous transfer. |
| 8 | Checked admission before I/O, deterministic NACK/timeout/error outcomes, release on every acquired-handle path and retained ownership after failed release. Test assertions cannot be disabled with NDEBUG. |

The SDK application descriptor header uses C11 static assertions. It is included
only in the already-approved C11 board entry point, which passes the 32-byte
image hash to the C99 application for copying. No language exception is expanded.

The automated gate and compiler tests complement this manual review. No vendor
source, dependency revision or approved deviation is changed.

## D4 manual BARR-C review and validation

Reviewed 2026-10-12: new `discuart` / `uartdisc`, extended `cfgdisc` and its
header, `c3app`, `cfgbind`, and `disctest` integration.

| Rules | Review result |
|---|---|
| 1.1, 1.5?1.8 | C99 retained; no new language, vendor or ABI exception. Contexts are serialized and borrowed callbacks retain lifetime. Enum casts map the documented bounded wire codes; time/count casts are bounded before conversion. |
| 1.3?1.4, 2?3 | Pinned formatter and LF/final-blank-line rules applied. Comments distinguish receive-only payload behavior, TX-idle configuration, completion timing and conservative matching. |
| 4 | Matching headers, static helpers, separate observer/transport/catalogue/UI responsibilities. Existing parser and UART owner are reused. |
| 5 | Explicit unsigned fields and little-endian encoding, bounded buffers, no structure serialization, no arithmetic counter overflow within the 2000-read bound. |
| 6?7 | Named callback typedefs, bounded functions/loops, initialized reports/contexts, no heap, no ISR or task ownership changes. Wait yields through the existing kernel API. |
| 8 | Busy/stale/invalid checks precede I/O; partial-open and failed-close ownership retained, explicit recovery, no TX calls or implicit retry. NDEBUG cannot disable test assertions. |

The mandatory gate checks 190 owned C/H files with zero findings or tool/policy
errors. Fresh application and fixture builds include the new module; physical
D4 acceptance remains not tested. See [D4 evidence](../../qualification/esp32c3-discovery-d4-20261012.json).
Earlier D2 evidence remains historical and unchanged.
