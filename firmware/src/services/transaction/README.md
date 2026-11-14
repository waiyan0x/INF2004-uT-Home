# Runtime transaction service

`TJ` v2 is the maintained rule-based program for the professor-approved
uT-Kernel/MQTT workflow. The host selects communication details from reviewed
peripheral Markdown. The ESP validates and stores one complete rule, then
selects its generic bus operation when explicitly asked to execute.

| Component | Responsibility |
|---|---|
| [txjob.c](txjob.c), [public header](../../../include/uthome/txjob.h) | Rule admission, request identity, execution state, exact-retry cache and raw replies |
| [txport.c](txport.c) | Generic I2C/SPI/UART operations and checked resource release |
| [c3tx.c](../../../app/c3tx.c) | Serialized application owner, MQTT copies and configuration arbitration |
| [Network](../../network/README.md) | TLS MQTT reception/publication; callbacks never invoke a driver |
| [Host tools](../../../../tools/transaction/README.md) | Markdown compilation, wire codec and explicit controller requests |

## Ownership and supported operations

An update performs no bus transfer. Invalid updates preserve the previous rule.
Execution checks boot identity, device, revision and current plan generation. Plan changes
invalidate rules; rules do not persist over reboot. One owner serializes calls,
with no heap, new task or kernel object. Requests and replies are copied; the
component retains no borrowed MQTT/sample pointers.

The service requires an explicitly idle peripheral plan and no admitted config
upload. It never stops another worker automatically or consumes a sensor FIFO.
USB/HTTP configuration waits while an asynchronous transfer or failed cleanup
retains the hardware. Cleanup failure keeps ownership until release succeeds.

| Protocol | Current board binding | Transfer shape |
|---|---|---|
| I2C | I2C0, SDA0/SCL1; 7-bit address 0x08-0x77 | Up to 30 TX / 32 RX bytes; implemented raw write/read shape, no inserted wait |
| SPI | SPI2, MOSI6/SCLK7; reviewed CS and optional D/C pins | Up to 30 TX bytes; receive unsupported |
| UART | UART1, RX20/TX21, 8N1 | Up to 30 TX / 32 RX bytes; incremental raw chunks, no frame alignment guarantee |

Timeout is 10-1000 ms. I2C clock is 10-400 kHz, SPI 100 kHz-8 MHz, UART
9600-460800 baud. Initialization, register side effects and required timing still
need peripheral review. SPI controller completion does not prove panel presence;
UART accepted bytes do not establish peripheral receipt. Zero-valued bytes are
valid results. Failure returns no usable RX bytes from an earlier operation.

## MQTT and session identity

Node-specific topics remain `uthome/v1/<node>/transaction/request` and `/result`;
that topic namespace is independent of the binary payload version. Both carry
exactly 96 bytes, QoS 0, non-retained. The transport rejects retained, DUP,
wrong-QoS and malformed messages. Use one controller and one outstanding command.

A read-only status query discovers the node's boot identity, current plan
generation, active rule, readiness, busy/cleanup state and request history. Node
identity is the MAC-derived topic identity, also returned by authenticated HTTP.
Boot identity is the **same nonzero 64-bit startup value** used by HTTP/cfgmux.
A broker reconnect does not change it. Zero is allowed only as a query's discovery
sentinel; command boot zero or a different boot is rejected before driver I/O.

Queries have independent nonzero correlation nonces and do not consume command
IDs, load rules, clear results or perform peripheral I/O. They use the owner's
existing separate output buffer, so a query during an asynchronous operation
cannot overwrite that operation's eventual completion. Normal owner polling
continues independently. Malformed queries are dropped; overlapping non-query
commands are not admitted. Query the pending ID before resolving an unknown
outcome instead of assuming a timeout means failure.

| Event | Required behavior |
|---|---|
| Normal command | Match topic/node, boot, generation, nonzero increasing request ID and operation kind; execute additionally matches active device/revision |
| Exact last-completion retry | Identical 96 bytes with the same current boot/generation returns the original cached reply, including its completion time; no new I/O |
| Changed or older command ID | Reject as stale; preserve the active rule and last completion |
| Host restart | Query state; recover the exact pending packet/outcome from durable host state before deciding what is known. Never restart command IDs at one or automatically repeat a write |
| ESP restart | New boot, no rule, no command history. Old-boot requests cannot execute even if generation and ID happen to match; explicitly load again |
| Plan generation changes | Previously loaded rule becomes unusable. Status hides its identity/settings immediately; next command clears it. ID history remains per boot; old-generation requests reject |
| Broker/Wi-Fi loss | Cancel pending work, finish checked cleanup and discard queued transport commands. Preserve completion cache; reconnect does not execute again |
| ID `0xffffffff` used | Queries and exact last retries still work; new commands cannot wrap. Stop and arrange an explicit fresh ESP boot/session before more commands |
| Completion no longer cached | Report outcome unknown; do not reconstruct success from the active rule or issue a replacement write |

Accepted commands include validated-envelope rejections (for example an invalid
candidate or non-idle plan), which consume that ID and can be retried exactly.
Malformed envelopes, wrong boot/generation, stale IDs, queries and overlapping
commands do not consume another ID. The status high-water mark is the maximum of
last completed and pending IDs. Only one completion is cached, and only in RAM.

Reply completion time is milliseconds on the ESP's boot-relative clock, recorded
after checked release, **not wall time or a fresh sensor timestamp on retry**.
Status also returns its observation time and the cached completion's generation.
The host must retain operation kind, boot, generation, counts and returned bus
settings. Only a successful execution supplies usable raw bytes; update ACKs and
status replies are not sensor measurements. Wrong-boot/kind/ID replies must not
complete the current request. A failed command may report the previous rule or
no rule. An unknown transport outcome stays unknown.

## TJ v2 wire layout

This is an intentional break from v1: old packets and version-1 transaction JSON
are rejected. Multi-byte integers are unsigned little-endian, encoded by bytes,
never native C structure packing. Device is 1-15 ASCII letters, digits, `_` or `-`,
NUL-terminated in 16 zero-padded bytes; overlong identifiers are rejected, never
truncated. Shortening this field keeps the wire buffers at 96 bytes while adding
session identity and completion time. TX 30 / RX 32 limits are unchanged.

Common header, requests and replies:

| Offset | Meaning |
|---|---|
| 0-2 | ASCII `TJ`, payload version 2 |
| 3 | 1 update, 2 execute, 3 status; reply kind 0 only for invalid command envelope |
| 4-7 | Command ID or query nonce; invalid-envelope reply uses 0 |
| 8-15 | Expected/current boot identity; query alone may request 0 |
| 16-19 | Expected/current plan generation; query request uses 0 |
| 20-23 | Candidate/expected or active revision; query request uses 0 |
| 24-39 | Candidate/expected or active device; query request uses all zero |

Update request body (execute and query requests zero **all** bytes 40-95):

| Offset | Meaning |
|---|---|
| 40-43 | Protocol (1 I2C, 2 SPI, 3 UART), address, mode, D/C pin |
| 44-49 | Rate u32, timeout ms u16 |
| 50-53 | TX length, RX length, CS pin, D/C level |
| 54-55 | Reserved zero |
| 56-85 | Up to 30 TX bytes; unused tail zero |
| 86-95 | Reserved zero |

All replies share these selected-rule fields (all zero if unconfigured):
40 protocol; 84 address; 85 mode; 86 CS; 87 D/C pin; 88-91 rate;
92-93 timeout; 94 D/C level. Byte 41 is status; byte 95 is reserved zero.
I2C/UART set mode/D/C level to zero and CS/D/C pins to 255. Supported SPI CS/D/C
pins are 3, 4, 5 or 10 and must be distinct; D/C may be absent (255).

Update/execute reply body:

| Offset | Meaning |
|---|---|
| 42-43 | Actual accepted TX and successful RX counts; update uses zero |
| 44-75 | Up to 32 raw RX bytes; unused tail zero; failure has no usable RX |
| 76-83 | Original completion time u64 in ESP boot-relative milliseconds |

Status reply body:

| Offset | Meaning |
|---|---|
| 42-43 | Reserved zero |
| 44-47 | Last completed command ID, or zero |
| 48-55 | That completion's original time, or zero |
| 56-59 | Pending command ID, or zero |
| 60 | Flags: loaded=1, busy=2, cleanup pending=4, idle-plan/no-upload/not-busy readiness=8 |
| 61-62 | Cached command kind and status, or zero if no completion |
| 63-64 | Active rule's configured TX/RX lengths, or zero |
| 65-67 | Reserved zero |
| 68-71 | Cached completion's generation, or zero |
| 72-79 | Current observation time u64 |
| 80-83 | Reserved zero |

Status values: 0 OK, 1 invalid, 2 unconfigured, 3 mismatch, 4 timeout, 5 I/O,
6 busy, 7 unsupported, 8 stale ID, 9 cancelled. A query with a nonzero wrong
expected boot returns mismatch with current identity; discovery uses boot zero.

## Verification and C review

Run the [host workflow and tests](../../../../tools/transaction/README.md).
`test_txjob` independently checks admission and state; `test_tx_owner` composes
the actual owner, adapters and generic HALs with bus doubles; `test_descriptions`
checks Markdown through that same C interface. Full target compilation, memory
checks and physical MQTT/three-bus acceptance are separate requirements.

Session review covers `txjob`, `c3tx`, `c3net`, `c3app`, their headers and the
`txptest` fixture. The module templates and existing C99 profile were retained.
Manual BARR-C review: byte access follows envelope length checks; name/packet
loops are bounded at 16/96; wide integers are split using explicit unsigned
shifts/masks; no unaligned casts, allocator, task, ISR or new kernel object was
introduced. State/requests/results remain copied and owner-serialized. A query
borrows const state and a documented disjoint caller buffer, leaving driver and
cache ownership unchanged. Boot identity is created before transaction startup.
Wrong boot/generation is checked before execution; invalid candidates preserve
the rule; cleanup failure retains the lease; cached bytes preserve timestamps.
No deviation, formatter pin, vendor revision or memory reservation changed.

The status reply adds no persistent buffer. Only the 64-bit boot value is new
state; shortening both stored rule identities releases more storage than that
cost. See the fresh target map and
[resource audit](../../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment)
for sizes, budgets and remaining TLS/TCP/IP allocation blockers. Passing the C
and map gates does not establish runtime stack/heap or physical bus acceptance.

Physical end-to-end MQTT and Home Assistant acceptance remain open. The host
controller and Home Assistant adapter are future work; no native API listener
remains.
