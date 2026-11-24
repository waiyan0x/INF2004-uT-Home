# Runtime transaction tools

This is the maintained host path from reviewed peripheral Markdown to the
ESP32-C3's `TJ` v2 rules. It uses the existing uT-Kernel firmware and MQTT.
The program is rule-based; it does not train weights or interpret sensor values.

| File | Purpose |
|---|---|
| `description.py` | Strictly parse one `peripheral-json` block, select an operation and emit transaction JSON plus source provenance |
| `client.py` | Validate transaction JSON, encode/decode the fixed wire format and send an explicit update/execution over TLS MQTT |
| `controller.py` | Own connection state, automatic IDs/revisions, selected/active rules, pending commands and display freshness |
| `journal.py` | Save exact commands and outcomes atomically, with an exclusive local SQLite lock |
| `session.py` | Run the controller continuously with explicit JSON commands on standard input |
| `homeassistant.py` | Publish read-only Home Assistant discovery and display state from controller snapshots |
| `requirements.txt` | Pinned host MQTT dependency |
| `requirements-dev.txt` | Additional pinned local TLS broker and certificate dependencies for tests |

Reviewed descriptions live in [datasheets](../../datasheets/README.md).
Handwritten three-protocol examples live in [configs/transactions](../../configs/transactions).
The [firmware contract](../../firmware/src/services/transaction/README.md) defines
bounds, ownership, errors and wire fields. Complete node profiles are different
objects; use the USB configuration tools for setup and recovery. The ESP HTTPS
service was removed on 17 November 2026; MQTT still verifies broker TLS.

## Compile without equipment

Run from the repository root:

```powershell
python -m tools.transaction.description datasheets/bmp388.md --list
python -m tools.transaction.description datasheets/bmp388.md --operation chip_id --revision 1 --address 0x77 --output build/transactions/a.json
python -m tools.transaction.description datasheets/bmp388.md --operation pressure_raw --revision 2 --address 0x77 --output build/transactions/b.json
python -m tools.transaction.client build/transactions/a.json --boot-id 0x1122334455667788 --generation 4 --request-id 1 --execute --output build/transactions/packets.json
```

The final command writes packet bytes to a report; it does not contact a board.
The boot identity, generation and address above are examples, not discovery of the current
hardware. I2C register descriptions use 100 kHz, a 100 ms timeout and a 1-16 byte
read. Schema 1.1.0 also compiles reviewed SPI writes and UART receive operations
to the same v2 wire format. Their descriptions specify clock or baud and timeout.
Omit `--address` for these operations. For example:

```powershell
python -m tools.transaction.description datasheets/ssd1306.md --operation display_off --revision 1 --output build/transactions/oled.json
python -m tools.transaction.description datasheets/ld2450.md --operation raw_32 --revision 2 --output build/transactions/radar.json
```

A `.report.json` sidecar records the Markdown/source hashes, operation, source
pages, configured bytes/counts and `hardware_executed=false`. Optional
`--datasheets ABSOLUTE_PDF_FOLDER` verifies the referenced source PDF hash.
Descriptions do not execute Markdown prose or call an online AI service.
Unsupported timing, initialization or framing must not be silently ignored.

## Prepare a live session

Install `requirements.txt` in the host environment. Confirm the installed image,
fixed wiring, node identity and current plan generation. Transactions require an
idle peripheral plan. Preserve the normal saved profile before explicitly
applying an empty plan through the existing configuration owner. A successful
Apply also replaces the saved profile: restore the exported profile explicitly
afterward; reboot alone does not restore it. Follow the
[configuration procedure](../config_transfer/README.md). The transaction
controller never changes the plan, saved settings, wiring or installed image.

Every plan-generation change closes MQTT and discards queued transport work.
Reconnection includes TLS and a new subscription; recent connection failures
can leave up to 30 seconds of retry backoff even for an intentional plan change.
After applying the idle plan, observe fresh MQTT status and readiness before
sending a rule. Wi-Fi connectivity and a successful profile Apply do not prove
that MQTT is responding. Do not bypass readiness or replay an uncertain command
to continue a demonstration.

Provision the board using the [network guide](../../firmware/src/network/README.md).
Set `UTHOME_MQTT_USER` and `UTHOME_MQTT_PASSWORD` privately on the host.
Keep CA files, credentials and journals outside tracked source, normally under
ignored `build/`. No credentials are stored in the controller journal.

## Persistent controller

Run from the repository root, replacing the uppercase placeholders and example
node with the actual broker, CA and MAC-derived node identity:

```powershell
python -m tools.transaction.session --broker BROKER --ca BROKER_CA_FILE --node uthome-0123456789ab --state build/controller/uthome-0123456789ab.sqlite3
```

The process queries status and maintains the connection. Enter one JSON object
per line; read each reply before the next operation:

```json
{"action":"select","path":"datasheets/bmp388.md","operation":"chip_id","address":119}
{"action":"load"}
{"action":"execute"}
{"action":"select","path":"datasheets/bmp388.md","operation":"pressure_raw","address":119}
{"action":"load"}
{"action":"execute"}
{"action":"status"}
{"action":"quit"}
```

These operations require the reviewed BMP388 wiring/address and an idle plan.
The raw-pressure operation can legitimately return zeros without initialization;
it does not establish pressure accuracy. Selection preserves the description
hash and source/page provenance but does not verify the original PDF. Use the
standalone compiler's `--datasheets` option when that verification is required.

| Action | Effect |
|---|---|
| `select` | Validate a Markdown operation and preview its complete rule; no peripheral I/O |
| `select_rule` | Preview a complete `rule` JSON object, such as an existing I2C/SPI/UART example; unsupported shapes reject locally |
| `load` | Assign a revision and command ID, save the pending packet, then request rule replacement; no peripheral I/O |
| `execute` | Request one transaction using the **acknowledged active rule** |
| `status` | Query boot, generation, readiness and history; no peripheral I/O |
| `recover` | Query status, then fetch an exact cached completion only when the same command is proven completed in the same session |
| `reconnect` | Reconnect and query; never load or execute automatically |
| `quit` / EOF | Close the connection and journal; retain any unknown outcome |

`select_rule` accepts `{"action":"select_rule","rule":{...}}`; use the complete
object from [the examples](../../configs/transactions), not a file path. The
controller assigns its revision when loading. Selecting a preview does not
replace the active rule. A rejected update preserves the previous active rule,
so a later explicit `execute` still uses that acknowledged rule. The output
keeps `selected` and `active` separate.

Output is JSON lines: `started`, `command`, `status`, `error` or `fatal`, with a
copied controller state. `last` is a historical receipt or an explicitly unknown
outcome. Displays must use `display.available` and `display.raw_hex` for current
bytes. Failure, uncertainty, disconnection or expiry makes that display
unavailable. Zero bytes remain valid. The default freshness limit is 5 seconds;
`--freshness-ms` accepts 100–60000. The age includes the status query round trip
and uses the ESP's original completion time, including for a cached reply.

One worker owns the controller, SQLite and MQTT calls. It services keepalives,
queries status approximately once a second and reconnects with 1–30-second
backoff. A pending command is never automatically resent. Input lines are
limited to 64 KiB and queued commands to eight. Errors or rejected transactions
make the eventual process exit status nonzero.

### Restart and uncertain outcomes

Always reopen the **same journal for the same node**. SQLite uses an exclusive
local-file lock and `DELETE`/`EXTRA` synchronous commits. The exact packet is
committed before publication; a crash after publication leaves an unresolved
record for explicit recovery. See SQLite's
[locking](https://www.sqlite.org/pragma.html#pragma_locking_mode) and
[durability settings](https://www.sqlite.org/pragma.html#pragma_synchronous).
Use a local filesystem, not a network share. Disk failure blocks further
commands; close and reopen after resolving it. Tests inject process termination
and write failure; physical host power-loss behavior is not qualified.

After a timeout, use `status` and explicit `recover`. If the ESP proves the
pending command is its cached completion, the identical 96-byte request retrieves
that receipt without new I/O. If it is unseen, still running or no longer cached,
the outcome remains unknown and new commands remain blocked. A status-reported
active rule alone cannot prove an update succeeded because status omits the full
outgoing bytes. Never delete the journal or use the one-shot client to bypass
an unknown outcome.

A verified fresh ESP boot clears active rules and permits new IDs; a plan
generation change invalidates the previous rule. Both require another explicit
load. An earlier unresolved outcome remains labelled unknown. The journal keeps
the current session and last outcome, not an audit history of every transaction.
A missing journal for an already used board session, foreign command history or
exhausted IDs blocks new work until an explicit fresh session is established.
The controller does not reboot the ESP to establish that session.

Python integrations can call `Controller.connect`, `select`/`select_rule`,
`load`, `execute`, `recover`, `refresh`, `poll`, `snapshot` and `close` from one
serialized owner. Call `poll` regularly. UI and Home Assistant should consume
the copied snapshot; do not mutate controller internals or create another
command owner.

The [Configuration Desk Peripheral transactions tab](../configurator/README.md#peripheral-transactions)
provides selection, Load rule, Execute active rule and result display using this
same controller. Start the Desk with the broker, CA, node and journal settings,
then connect explicitly in the tab. Close any command-line owner for that node
first. One worker owns the controller and optional HA publisher; browser requests
consume copied snapshots and queue explicit actions.

## Home Assistant display

Add `--home-assistant` to the same session command. It uses the same broker,
CA and private `UTHOME_MQTT_USER` / `UTHOME_MQTT_PASSWORD` settings, with a
separate display connection. The controller remains the only command owner.

```powershell
python -m tools.transaction.session --broker BROKER --ca BROKER_CA_FILE --node uthome-0123456789ab --state build/controller/uthome-0123456789ab.sqlite3 --home-assistant
```

Configure Home Assistant's MQTT integration to use that broker and its default
`homeassistant` discovery prefix / `homeassistant/status` birth topic. The
adapter registers one device with six sensors: operation status, active rule,
protocol, raw bytes, TX bytes accepted and RX bytes received. It publishes
display state only; it creates no buttons, command subscriptions or automatic
peripheral operations. Follow the earlier `select`, `load`, `execute` sequence
to switch BMP388 operations. Merely enabling the display or selecting a preview
does not contact a peripheral.

The host account needs these **additional** broker permissions for its own node;
keep the existing transaction request/result permissions:

| Direction | Topic | Purpose |
|---|---|---|
| Publish | `homeassistant/sensor/NODE/+/config` | Six retained discovery definitions |
| Publish | `uthome/v1/NODE/display/state` | Non-retained JSON state, QoS 0 |
| Publish | `uthome/v1/NODE/display/availability` | Retained online/offline and last will, QoS 1 |
| Subscribe | `homeassistant/status` | Re-announce on Home Assistant startup/reload |

Replace `NODE` with the MAC-derived identity. Home Assistant needs to read these
discovery/state/availability topics and publish its birth status. The adapter's
client ID is `NODE-display`; run only one display adapter per node. MQTT 3.1.1
cannot reliably report denied publications to the sender, so confirm broker
permissions if the session reports connected but no entities appear.

The active rule is the ESP-acknowledged rule, independent of the selected preview.
The operation-status sensor's attributes separate `configured` TX bytes/counts
and requested RX length from `last_result` counts reported by the ESP. They also
include boot identity, generation, request ID and `completed_ms` (milliseconds
since ESP boot, **not UTC**). The raw/count sensors use only the controller's
fresh successful execution result. An update acknowledgement, timeout, unknown
outcome, disconnection or expiry clears those values. Zero bytes are valid; a
successful write with no received bytes has an empty raw string and RX count 0.

State has a half-second refresh interval when the owner can service it and is
republished after discovery; meaningful changes publish on the next poll. Each HA
sensor expires after two seconds without state updates, bounding stale display
when the host stops or is busy waiting for a command. Sample freshness is still
decided by the controller's original completion time, with normal network/HA
delivery latency. Before load/execute/recovery the session clears the displayed
raw result. A display connection failure emits a `display` event and retries
with 1–30-second backoff; it does not replay a command or stop the controller.

Discovery and adapter availability are retained; result state is not. A new
display connection discards old queued messages and publishes a current snapshot.
Home Assistant's `online` birth re-announces discovery and state without loading
or executing a rule. Reopened journal data alone cannot make the ESP online.
This follows Home Assistant's [discovery lifecycle](https://www.home-assistant.io/integrations/mqtt/#discovery-messages-and-availability)
and [sensor availability/expiry](https://www.home-assistant.io/integrations/sensor.mqtt/).

Host tests verify this contract through a local TLS broker and the actual C owner
with simulated buses. Physical ESP/Desk/Home Assistant runs verified BMP388 and
LD2450 switching, expiry and reconnect behavior. See the
[physical verification summary](../configurator/README.md#physical-verification)
and [presenter guide](../configurator/README.md#tribunal-2-presenter-guide).

For a live session, confirm the board/image, idle plan, wiring and current MQTT
readiness. Loading alone leaves raw bytes unavailable. Keep the same journal
across host restarts; recovery must not add a peripheral operation. Requalify
network or certificate changes against the [resource scope](../../docs/reports/wifi-static-memory/README.md#qualification-limits).

## One-shot codec and diagnostic client

The low-level client remains useful for packet inspection and isolated tests:

```powershell
python -m tools.transaction.client --status --request-id QUERY_NONCE --broker BROKER --ca BROKER_CA_FILE --node uthome-MAC_ID
python -m tools.transaction.client build/transactions/a.json --boot-id OBSERVED_BOOT --generation CURRENT_GENERATION --request-id NEXT_ID --execute --broker BROKER --ca BROKER_CA_FILE --node uthome-MAC_ID
```

Replace uppercase placeholders with observed/private settings. `--status` performs
no peripheral I/O and consumes no command ID; use its `boot_id`, `generation`,
`last_request_id` and readiness before a command. `--boot-id` accepts decimal or
`0x` hex. This one-shot CLI does not use the persistent journal or automate
recovery; never run it as a second command owner alongside the controller.
IDs cannot wrap at 32 bits. The client checks
broker CA and hostname, subscribes before publishing, sends one update and sends
execution only after a successful update. Each `--execute` run uses two IDs.
Use increasing nonzero request IDs within one board boot and one controller.
Do not assume restarting the host restarts the board's ID history. The codec can
construct an exact repeat of the last request; it never automatically retries.
A transport timeout means the outcome is unknown, particularly for writes.

Replies retain operation kind, boot, active identity/revision, generation,
actual TX/RX counts, selected address/rate/mode/pins/timeout, completion time and
raw hex. A cached retry keeps the original boot-relative completion time.
`result()` checks the requested kind/boot/ID when supplied and validates returned
settings. The MQTT exchange always checks these fields, drops delayed or
mismatched replies and never automatically resends a command. Status decoding
is separate from raw transaction results. Rejection may identify the previous rule or an unconfigured state;
request ID, boot and operation kind are still checked. Successful replies also must match the expected
identity and generation. Failed operations return a nonzero CLI exit status.

## Verification

Install `requirements-dev.txt` and put GCC on PATH. These tests need no equipment
or Wi-Fi settings; the broker binds only to loopback with fresh temporary test
certificates/passwords, verified TLS and topic-specific credentials:

```powershell
$env:PYTHONPATH = 'tests/target;.'
python -B -m unittest test_descriptions test_transaction_client test_transaction_controller test_transaction_mqtt test_homeassistant test_txjob test_tx_owner
```

The Markdown parity test loads one real C owner/HAL fixture, switches BMP388
operations, checks valid zero bytes, rejects an oversized target update, then
executes the previous rule. These are simulated bus observations, not board
acceptance. Host test libraries are unloaded before temporary files are removed.

Controller tests also terminate a host process after its journal commit, inject
lost replies/disk errors, change boot/generation during recovery, enforce one
journal owner across processes and expire old data. The real local TLS broker
test runs A -> B -> locally rejected update -> B -> host restart/cached recovery
-> broker restart -> A, checking that recovery adds no C driver call. Certificate
hostname and login rejection are exercised. An owned socket wrapper closes
failed TLS handshakes around the unchanged pinned Paho 1.6.1 dependency.
The pinned test broker currently warns about its legacy `acl` option; its typed
configuration still uses that field. The warning is not suppressed.

The display tests cover Markdown A/B/A changes, zero bytes, update versus
execution, rejected updates, failed reads, expiry, unknown outcomes and boot/
generation changes. Local-broker tests check retained discovery, non-retained
state, actual offline last will, host reopen, HA birth and broker restart without
another C driver call. They also exercise the public `--home-assistant` option
and show that display failure leaves explicit controller operations usable.
These are MQTT-contract tests, not rendered Home Assistant or hardware acceptance.
