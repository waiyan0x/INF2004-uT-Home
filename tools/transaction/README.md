# Runtime transaction tools

This is the maintained host path from reviewed peripheral Markdown to the
ESP32-C3's `TJ` v1 rules. It uses the existing uT-Kernel firmware and MQTT.
The program is rule-based; it does not train weights or interpret sensor values.

| File | Purpose |
|---|---|
| `description.py` | Strictly parse one `peripheral-json` block, select an operation and emit transaction JSON plus source provenance |
| `client.py` | Validate transaction JSON, encode/decode the fixed wire format and send an explicit update/execution over TLS MQTT |
| `requirements.txt` | Pinned host MQTT dependency |

Reviewed descriptions live in [datasheets](../../datasheets/README.md).
Handwritten three-protocol examples live in [configs/transactions](../../configs/transactions).
The [firmware contract](../../firmware/src/services/transaction/README.md) defines
bounds, ownership, errors and wire fields. Complete node profiles are different
objects; keep using the configuration tools for setup and recovery.

## Compile without equipment

Run from the repository root:

```powershell
python -m tools.transaction.description datasheets/bmp388.md --list
python -m tools.transaction.description datasheets/bmp388.md --operation chip_id --revision 1 --address 0x77 --output build/transactions/a.json
python -m tools.transaction.description datasheets/bmp388.md --operation pressure_raw --revision 2 --address 0x77 --output build/transactions/b.json
python -m tools.transaction.client build/transactions/a.json --generation 4 --request-id 1 --execute --output build/transactions/packets.json
```

The final command writes packet bytes to a report; it does not contact a board.
The generation and address above are examples, not discovery of the current
hardware. Markdown compilation uses reviewed I2C register reads, a 100 kHz bus
and a 100 ms timeout. Description schema 1.0.0 retains its 1-16-byte read limit;
the general `TJ` service independently allows up to 32 received bytes.

A `.report.json` sidecar records the Markdown/source hashes, operation, source
pages, configured bytes/counts and `hardware_executed=false`. Optional
`--datasheets ABSOLUTE_PDF_FOLDER` verifies the referenced source PDF hash.
Descriptions do not execute Markdown prose or call an online AI service.
Unsupported timing, initialization or framing must not be silently ignored.

## Send over MQTT

Install `requirements.txt` in the host environment. Confirm the installed image,
fixed wiring, node identity and current plan generation. Transactions require an
idle peripheral plan. Preserve the normal saved profile before explicitly
applying an empty plan through the existing configuration owner.

Provision the board using the [network guide](../../firmware/src/network/README.md).
Set `UTHOME_MQTT_USER` and `UTHOME_MQTT_PASSWORD` privately on the host. Then:

```powershell
python -m tools.transaction.client build/transactions/a.json --generation CURRENT_GENERATION --request-id NEXT_ID --execute --broker BROKER --ca BROKER_CA_FILE --node uthome-MAC_ID
```

Replace uppercase placeholders with observed/private settings. The client checks
broker CA and hostname, subscribes before publishing, sends one update and sends
execution only after a successful update. Each `--execute` run uses two IDs.
Use increasing nonzero request IDs within one board boot and one controller.
Do not assume restarting the host restarts the board's ID history. The codec can
construct an exact repeat of the last request; it never automatically retries.
A transport timeout means the outcome is unknown, particularly for writes.

Replies report status, active identity/revision, generation, accepted TX count
and raw hex. Rejection may identify the previous rule or an unconfigured state;
request ID is still checked. Successful replies also must match the expected
identity and generation. Failed operations return a nonzero CLI exit status.

## Verification and next work

With the pinned formatter and GCC available:

```powershell
$env:PYTHONPATH = 'tests/target;.'
python -B -m unittest test_descriptions test_transaction_client test_txjob test_tx_owner
```

The Markdown parity test loads one real C owner/HAL fixture, switches BMP388
operations, checks valid zero bytes, rejects an oversized target update, then
executes the previous rule. These are simulated bus observations, not board
acceptance. Host test libraries are unloaded before temporary files are removed.

The long-running controller session and Home Assistant discovery/display adapter
remain to be implemented here. The adapter will publish discovery and display
state from decoded replies; there is currently no Home Assistant display path
in the cleaned firmware. UI changes require the user's review.
