# Team-authored peripheral descriptions

These six Markdown files were prepared from the PDFs in the project's
`ESP/Datasheets` folder. Each contains a concise communication summary, PDF-page
references, exact source SHA-256, prerequisites and one `peripheral-json` block.
They are editable starting points for the team, not automatic PDF extraction
or a claim that every device has been tested on hardware.

| Description | Operations compilable to current description schema | Remaining limits |
|---|---|---|
| [BMP388](bmp388.md) | `chip_id`, `pressure_raw` | Select 0x76/0x77 from wiring; conversion setup is external |
| [ADXL345](adxl345.md) | `device_id` | I2C mode and address strap must match; host tests only |
| [VEML7700](veml7700.md) | `configuration_raw`, `ambient_raw` | Defaults to shutdown; no measurement initialization; host tests only |
| [SEN0390](sen0390.md) | None; `illumination_raw` is listed but rejected | Timing and register ambiguities require review |
| [SHT4x](sht4x.md) | None; `measure_high_precision` is listed but rejected | Separate command/wait/read sequence required |
| [LD2450](ld2450.md) | None; `raw_report` is listed but rejected | UART framing and 30-byte response required |

The compiler now produces the maintained `TJ` transaction format. Description
schema 1.0.0 supports one register byte followed by repeated START and an
immediate 1-16-byte I2C read. The broader firmware service allows more raw
transfer shapes; this description schema deliberately remains narrower.
Unsupported operations explain their missing timing or framing capability.
See the [host guide](../tools/transaction/README.md) and
[firmware contract](../firmware/src/services/transaction/README.md).

## Use the descriptions

Run from the `Revised` repository root. Inspect the supported/unsupported
operations first:

```powershell
python -m tools.transaction.description datasheets/bmp388.md --list
```

Compile the two operations from the same Markdown file, selecting the actual
seven-bit address and distinct nonzero rule revisions:

```powershell
python -m tools.transaction.description datasheets/bmp388.md --operation chip_id --revision 1 --address 0x77 --output build/markdown-proof/a.json
python -m tools.transaction.description datasheets/bmp388.md --operation pressure_raw --revision 2 --address 0x77 --output build/markdown-proof/b.json
python -m tools.transaction.client build/markdown-proof/a.json --generation 4 --request-id 1 --execute --output build/markdown-proof/packets.json
```

Each compile writes the rule JSON plus a `.report.json` sidecar containing the
Markdown/source hashes, selected operation and pages, notes, transmitted byte,
transmit/receive counts, and `hardware_executed=false`. The rule alone goes to
the existing host transport; the report stays on the host. The packet compilation is offline. The parity test uses the actual C owner
with simulated buses; neither establishes sensor accuracy.

Optionally add `--datasheets "ABSOLUTE_PATH_TO_ESP/Datasheets"` to either listing
or compilation. This verifies the PDF bytes against the recorded SHA-256 before
producing a rule. Otherwise the report says `source_pdf_verified=false`; PDF
files are not needed at runtime. Hash verification establishes source identity,
not semantic correctness. The source PDF files are outside this repository; only the reviewed Markdown
descriptions are maintained here.

Compilation never connects to USB or MQTT. Follow the host guide for explicit
MQTT delivery after confirming the installed image, current generation and wiring.
The compiler currently supplies 100 kHz I2C and a 100 ms transfer timeout.

## Prepare or edit a Markdown

Copy the closest description and retain exactly one top-level fenced block
tagged `peripheral-json`. Ordinary prose, tables and other code blocks are
documentation only. The parser does not infer executable fields from prose,
execute Markdown, load Python or contact an AI service.

The description schema is independently versioned `1.0.0`:

| Field | Meaning |
|---|---|
| `schema_version` | Exactly `"1.0.0"` |
| `device` | 1-31 ASCII letters, digits, `_` or `-`; logical device identity |
| `bus` | `i2c` or reference-only `uart` in this schema |
| `addresses` | Unique seven-bit I2C address choices, or empty for UART |
| `source` | PDF basename, lowercase SHA-256 and total PDF page count |
| `operations` | 1-16 uniquely named operations |

For an executable operation, the exact fields are `kind: i2c_register_read`,
`register` (integer 0-255), `read_length` (integer 1-16), `pages` (nonempty list of
one-based PDF page numbers), and `notes` (prerequisites, side effects and limits).
All JSON numbers are decimal; explanatory prose may use hexadecimal. The CLI
accepts decimal or `0x` notation for address selection. Do not put shifted
write/read address bytes into `addresses`.

For a deferred operation, use `kind: unsupported`, `reason`, `pages` and `notes`.
Explain its real sequence and byte counts in the prose. Do not relabel it as
`i2c_register_read` simply to make compilation succeed. Writes, delays, multi-step
sequences and SPI need a later description schema; transfer shapes absent from
the `TJ` service also need firmware work. Additional fields
such as `delay_ms` are rejected rather than silently ignored.

The CLI requires an explicit address when multiple choices exist; one choice
may be selected automatically. The description does not discover wiring,
configure bus pins/frequency or choose a physical source. The caller supplies
the revision; this version does not allocate revisions or enforce monotonicity.
Unknown keys, duplicate JSON keys, multiple/unterminated blocks, invalid types,
out-of-range values, missing source references and files over 64 KiB are rejected.
Rejection during admission leaves existing output files unchanged.

Before enabling an operation, check the exact part/revision, seven-bit address,
bus selection, pointer/command bytes, read length, STOP/repeated-START behavior,
timing, initialization and read side effects against the referenced PDF pages.
These communication constraints matter even though physical-unit decoding and
data accuracy are not this prototype's objective. A reviewed Markdown remains
team-supplied configuration; the parser cannot prove its claims about a device.

## Verification and ownership

```powershell
$env:PYTHONPATH = 'tests/target;.'
python -B -m unittest test_descriptions test_transaction_client test_txjob test_tx_owner
```

Tests cover all six descriptions, strict schema/provenance checks, unsupported
operations, rejected-output preservation and Markdown-to-C runtime switching.
Member 4 owns the description contract/compiler; driver owners review bus
semantics; Member 5 owns the controller workflow; Member 2 owns transport.
See the [team workload](../CONTRIBUTING.md).
