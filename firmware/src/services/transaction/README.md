# Runtime transaction service

`TJ` v1 is the maintained rule-based program for the professor-approved
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
Execution checks device, revision and current plan generation. Plan changes
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

## MQTT and request identity

Node-specific topics are `uthome/v1/<node>/transaction/request` and `/result`.
Both carry exactly 96 bytes, QoS 0, non-retained. The receiver rejects retained,
DUP, wrong-QoS and malformed messages. One command can be pending; no automatic
replay is promised. Use one controller with increasing nonzero request IDs per
boot. An exact repetition of the last completed request returns its cached reply
without repeating I/O; changed/older IDs are rejected.

Disconnect cancels unfinished work and discards queued transport commands.
A missing reply is an unknown outcome, not permission to repeat a write with a
new ID. Wrong generation/revision must not touch hardware. Rejected replies can
name the previous active rule or no rule; the host must report that status.

## TJ v1 wire layout

All multi-byte integers are unsigned little-endian; the wire never uses native
C structure packing. Device is 1-31 ASCII letters, digits, `_` or `-`, NUL-terminated
in 32 zero-padded bytes. Unused and reserved bytes must be zero.

| Offset | Request | Reply |
|---|---|---|
| 0-2 | ASCII `TJ`, version 1 | Same |
| 3 | 1 update, 2 execute | Request kind; 0 if envelope invalid |
| 4-7 | Request ID | Request ID; 0 if envelope invalid |
| 8-11 | Candidate/expected revision | Active revision |
| 12-43 | Device identity | Active device |
| 44 | Protocol: 1 I2C, 2 SPI, 3 UART | Active protocol |
| 45 | I2C address | Status |
| 46 | SPI mode | Accepted TX length |
| 47 | D/C pin, 255 absent | Successful RX length |
| 48-51 | Bus rate | RX bytes start at 48; at most 32 |
| 52-53 | Timeout ms | RX area |
| 54-57 | TX length, RX length, CS pin, D/C level | RX area |
| 58-59 | Reserved | RX area |
| 60-79 | TX data start at 60 | RX area ends at 79 |
| 80-82 | TX area | Active address, mode, CS pin |
| 83 | TX area | Reserved |
| 84-87 | TX area | Active rate |
| 88-89 | TX area ends at 89 | Active timeout ms |
| 90-93 | Expected plan generation | Current plan generation |
| 94-95 | Reserved | Reserved |

Execute requests zero bytes 44-89. Status values are 0 OK, 1 invalid,
2 unconfigured, 3 mismatch, 4 timeout, 5 I/O, 6 busy, 7 unsupported,
8 stale ID and 9 cancelled. I2C/UART set SPI-only fields to zero, with CS/D/C
pins 255. Supported SPI CS/D/C choices are 3, 4, 5 or 10 and must be distinct.

## Verification and C review

Run the [host workflow and tests](../../../../tools/transaction/README.md).
`test_txjob` independently checks admission and state; `test_tx_owner` composes
the actual owner, adapters and generic HALs with bus doubles; `test_descriptions`
checks Markdown through that same C interface. Full target compilation, memory
checks and physical MQTT/three-bus acceptance are separate requirements.

Cleanup review covers `c3app`, `c3net`, `c3tx`, `txjob`, `netread`, `c3https` and
`c3mqtt`: removed USB-lab/native-display branches and discovery formatting;
wire bytes and generic driver behavior are unchanged. MQTT reply storage remains
copied and fixed-size. Exact retry, cancellation, timeout and checked release
retain the same owner. The unused display-cache timestamp was removed along
with its private function parameter; wire layout and retry contents are unchanged.
HTTPS now always uses its bounded TLS implementation.
No new casts, language deviation, allocator, ISR work or task is introduced.
The pinned formatter, full C gate, warnings-as-errors tests and fresh target
build are required; the lexical gate alone is not a complete BARR-C review.

Physical end-to-end MQTT and Home Assistant acceptance remain open. The host
Home Assistant adapter is future work; no native API listener remains.
