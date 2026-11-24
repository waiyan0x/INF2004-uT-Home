# Local configurator

From the repository root:

```powershell
python -m pip install -r tools/requirements.txt -r tools/config_transfer/requirements.txt
python -B -m tools.configurator --port 8080
```

Open `http://127.0.0.1:8080`. Stop the server with Ctrl+C. The UI uses Python's
standard HTTP server and local HTML/CSS/JavaScript; no Node build or new web
framework is required. Offline review needs only the planner dependencies.
The interface uses one fixed colour palette. Removing the ESP HTTPS service
does not change this host web interface. Full-profile setup uses USB; runtime
transactions use MQTT over TLS.

## Peripheral transactions

The Desk opens on **Peripheral transactions** for I²C, SPI and UART operations. Profile, Review & apply and Live monitor keep their existing workflows.
On the opening tab, use **Connect MQTT** for wireless transactions or enter the board's serial port and select **Connect USB** for profile setup and diagnostics. Descriptions can be previewed offline.

To enable runtime actions, install the transaction requirements and supply the
same local settings used by the [controller session](../transaction/README.md#persistent-controller):

```powershell
python -m pip install -r tools/transaction/requirements.txt
python -B -m tools.configurator --port 8080 --broker BROKER --mqtt-port 8883 --ca BROKER_CA_FILE --node uthome-0123456789ab --state build/controller/uthome-0123456789ab.sqlite3 --home-assistant
```

Set `UTHOME_MQTT_USER` and `UTHOME_MQTT_PASSWORD` privately in that server's
environment. Omit `--home-assistant` if no display publisher is needed. Keep one
controller and one journal per node; close the command-line session before
connecting the Desk to the same node. Startup and opening the tab do not connect
to MQTT. Use **Connect** explicitly, after confirming the board, firmware, idle
plan and reviewed wiring for any physical trial.

The screen contains:

1. A session row: node, MQTT readiness, HA publisher state and Connect/Disconnect.
2. One selection card: reviewed Markdown description, operation and documented
   address. A read-only line shows protocol, outgoing bytes, requested RX length,
   rate and timeout, with relevant prerequisites beside the selection.
3. **Load rule**, followed by the acknowledged active operation/revision and
   **Execute active rule**. Selection alone changes only the preview; loading
   replaces a rule without performing peripheral I/O. Execution uses the active
   rule, even if another operation is selected in the preview.
4. One result card: operation status, fresh raw bytes, reported TX/RX counts and
   age.

For example, selecting `pressure_raw` previews TX `04` and RX length 3 while
`chip_id / revision 1` remains active. Execute still uses chip ID until Load
succeeds. After loading pressure and executing once, the three returned bytes
may legitimately be `00 00 00`; this does not establish pressure accuracy.

The current [controller](../transaction/README.md#persistent-controller) supplies
readiness, IDs, revisions, durable command ownership and freshness. One
server worker owns that controller and the optional HA publisher; the page
consumes copied state. Broker/CA/credentials and the per-node journal stay in
local server settings. The browser does not edit request counters, journal
paths, wire packets or credentials. Connecting is explicit and performs no
rule load or execution. HA publisher connectivity is not proof that HA rendered
or received a message.

| State | What the page shows or permits |
|---|---|
| Offline or not ready | Preview remains usable; Load/Execute disabled with the reason |
| Rule accepted | Active operation/revision updates; live raw bytes/counts remain unavailable |
| Execution complete | Current raw bytes/counts and age, including legitimate zero bytes |
| Expired, failed snapshot or disconnected | Live bytes and transfer counts clear |
| Unsupported operation | Explain its missing capability beside the preview; loading is disabled |
| Rejected rule | Show rejection; preserve the acknowledged active rule |
| Outcome unknown | Block Load/Execute; show Check status and Recover last result only as needed; never repeat uncertain I/O automatically |

The existing five-second result freshness limit applies. The display clears
before load/execute/recovery and when a snapshot is unavailable; copied state
expires after two seconds without a worker/browser refresh. Plan-generation or
boot changes invalidate the active rule through the controller. The runtime
view does not apply an idle plan or save startup rules automatically. Keep the
existing profile workflow for explicit setup/restoration.

On small screens, fields stack and the four tabs remain keyboard accessible.
The Runtime tab previews the SPI clock, mode, CS and D/C, or the UART baud.
Only I2C operations take an address. Unsupported operations stay unavailable. Normal actions need no JSON
editing. Each execution requires an explicit request.

`GET /api/runtime-catalogue` compiles only repository descriptions, without
peripheral I/O. Session-authenticated `POST /api/runtime` accepts `snapshot`,
`connect`, `disconnect`, `load`, `execute`, `status` and `recover`. A load names
the catalogue filename, operation, I2C address (null for SPI/UART) and reviewed description
SHA-256. Changed descriptions require another page review. Actions are queued
to one owner; HTTP handlers never access the journal or MQTT client directly.
The existing Host/Origin/session-token and request-size checks apply.



### Tribunal 2 presenter guide

Allow about six minutes to present and a separate setup window. Have the physical
ESP's Home Assistant device page beside the Desk, plus the software block diagram
and the [memory evidence](../../docs/reports/wifi-static-memory/README.md#week-13-evidence-for-aai13)
ready to show. The physical and simulated nodes have separate HA device pages.

**Prepare before presenting.** Use the reviewed BMP388 wiring at I2C address
`0x77` and LD2450 wiring at 256000 baud. Keep the original profile and a verified
full flash backup before a temporary firmware installation. Start HA, the broker
and a Desk session configured for the physical node and the trusted broker CA.
The physical rehearsal used separate broker connections for ESP commands and HA
display publication. Open the URL printed by that physical session. An offline
Desk can preview descriptions even when it has no MQTT target.

Use USB for profile setup. Apply and save the reviewed idle profile and verify
the active and saved revisions and CRCs match. The periodic sensor workers
must release the buses before runtime commands can use them. If using a bank,
disconnect laptop USB, allow power to drop, then power the setup from the bank
before connecting MQTT. Wait for the new boot to report ready. Complete any
readiness restart before the first load. Keep the board powered through the
entire I2C / UART / I2C sequence.

| Time | Operator action | What the audience should see or hear |
|---|---|---|
| 0:00 | Show diagram page 2. Point from Markdown to compiler, controller, broker, ESP and HA. | The host compiles the rule. The ESP validates and executes it. HA displays six read-only sensors. Page 3 shows why the transaction engine requires an idle peripheral plan. |
| 0:40 | In **Peripheral transactions**, click **Connect MQTT** once. | Look for **MQTT ready**, **HA publisher connected**, and the physical node ID. **Connect USB** is for profile management. |
| 1:00 | Select BMP388, `chip_id`, address `0x77`. Point out register `00`, one byte to read, bus speed and timeout. Click **Load rule** and pause. | The active rule becomes `chip_id` with a revision. Raw bytes and counts are unavailable. Loading validates and stores the rule without peripheral I/O. |
| 1:40 | Click **Execute active rule** once. Show Desk and HA together. | Hex `50`, TX accepted 1, RX received 1. In HA, show operation status, active rule, protocol, raw bytes and both counts. |
| 2:10 | Select LD2450 and `raw_16`. Pause before loading. Then load, wait for acknowledgement, and execute once. | The preview changes to UART at 256000 baud while the old rule remains active until load. UART needs no I2C address. TX 0, RX 16 shows a receive-only radar read. The chunk can start mid-frame. |
| 3:00 | Return to BMP388 / `chip_id` / `0x77`. Load and execute once. | Hex `50`, TX 1, RX 1 again. Both protocol changes ran on one boot and firmware image. |
| 3:30 | Leave the display alone for five seconds. Then click **Disconnect MQTT**. | First the result expires and raw/TX/RX clear while the active rule remains. After disconnect all six HA sensors become unavailable. |
| 4:10 | Show the memory table, C-standard check and recovery evidence. | Explain which properties the live reads show and which require the measurements and controlled tests below. |

**USB-powered wireless demonstration.** Keep the USB cable connected for power after setup.
Leave the Desk serial connection closed and show **USB disconnected**. Click **Connect MQTT**,
load the chip-ID rule and execute once. While `50` is fresh, **Latest result** shows the
matching MQTT/TLS reply's request ID, packet length and result topic. Its 96 bytes include the
transaction envelope carrying the one sensor byte. Keep the cable in place and click
**Disconnect MQTT**. The reading clears and execution becomes unavailable.

For a longer demonstration insert BMP388 `pressure_raw` after the first chip ID
(TX 1, RX 3), and LD2450 `raw_32` after `raw_16` (TX 0, RX 32). Pressure is a
raw register value. Radar data are bytes from a stream. The physical rehearsal
on 23 November 2026 returned `50`, `B5 21 6A`, 16 radar bytes, 32 radar bytes
and `50` in that order. Fresh runs may return different pressure and radar bytes.

**Requirement questions**

| ID | Evidence to show or explain |
|---|---|
| FR1 | Preview a reviewed operation before loading. Compare its register, byte counts, speed and timeout with the [BMP388 description](../../datasheets/bmp388.md). |
| FR2 | Show the acknowledged active rule before executing, then the same-boot BMP388 / LD2450 / BMP388 switch. |
| FR3 | The generic layer supports I2C write/read, SPI write and raw UART transmit/receive. Limits are 30 TX bytes, 32 RX where supported, and 10 to 1,000 ms. The live radar operation uses RX. [Bus tests](../../tests/target/test_tx_owner.py) cover UART TX and SPI write. SPI writes were acknowledged on the bench. Visible OLED operation under the idle plan remains unverified. |
| FR4 | Match raw bytes and counts across Desk and HA. For request ID, rule revision and ESP completion time, use the correlated result record or HA operation-status attributes. The age on the Desk is elapsed time. |
| FR5 | Show the USB port control, **Review & apply**, active/saved revisions and **Live monitor** during setup. Explain that reconnect queries boot, plan generation and last command before the controller can recover a matching cached result. See the [physical restart walkthrough](#physical-verification) and [controller tests](../../tests/target/test_transaction_controller.py). |
| NFR1 | Each TJ v2 command and reply is 96 application bytes over certificate-verified MQTT/TLS. One controller owns the journal, one rule is active, and at most one command is outstanding. |
| NFR2 | Invalid or stale changes preserve the old rule. A proven exact retry returns the cached reply. Link loss cancels active work and releases resources. Use the [compiled rule-engine tests](../../tests/target/test_txjob.py) and [owner tests](../../tests/target/test_tx_owner.py) for rejection, replay and cancellation. |
| NFR3 | All 41 modules fit. Fixed DRAM is 274,764 of 278,528 bytes, leaving 3,764. The separate diagnostic run attributed all 410 observed general allocations to documented Wi-Fi driver paths. Six runtime samples recorded zero new kernel objects. The qualification covers the single-broker demo workload. |
| NFR4 | Show five-second expiry from original ESP completion and disconnect. Failed or uncertain operations also clear current data. Recovered results retain their original age. |
| NFR5 | Show the pinned gate result alongside the [manual audit](../c_standard/AUDIT.md) and [deviation register](../c_standard/deviations.json). The automated gate checked 353 owned sources and headers with zero findings on 23 November. Manual review covers rules the checker cannot establish. |

**If the demo pauses.** For **Not configured**, open the physical session URL.
For an unavailable HA reading, check the device identity, publisher status and
result age. For **Outcome unknown**, use **Check status**, then **Recover last
result** only if offered. Avoid executing again merely to recover a reply.
After a reboot or plan change, wait for ready and explicitly reload the rule.
If the link cannot be restored during the presentation, identify the recorded
physical rehearsal as prior evidence. Finish the backed-up session by
disconnecting MQTT, returning the board to laptop USB and verifying restoration.

### Physical verification

Physical ESP32-C3, Desk and Home Assistant runs verified BMP388 chip-ID and raw
pressure reads, LD2450 16/32-byte reception, and return to BMP388 on one firmware
image. Chip ID was `50`; pressure and radar bytes depend on device state. Radar
chunks can cross frame boundaries. The 64-read repeated workload also completed.
A separate cold-start rehearsal ran from a power bank without laptop USB data.

The 23 November presenter rehearsal and 24 November campus-uplink rehearsal used
USB power and MQTT commands/results. The three-read presenter sequence returned
BMP388 `50`, 16 LD2450 bytes, and BMP388 `50` on one boot. Results expired while
the active rule remained; disconnect made all six HA entities unavailable.
Original flash/profile and temporary host settings were restored and verified.
Raw captures and laptop-specific launchers are private local artifacts, not
portable setup prerequisites. Use the public startup command above with your
own broker, trusted CA, node and journal.

Restart checks covered the Desk using the same journal, the command broker,
the display broker, HA Core and the ESP. Host/broker/display restarts did not
perform new bus operations or refresh an old completion timestamp. An ESP reboot
changed boot identity and cleared the rule; another explicit load was required.

SPI commands were accepted with the reported TX counts, but the panel remained
blank after the idle transition in operator-observed trials. Reapplying the
normal profile restored its display. Reset level, initialization and physical
waveforms remain unqualified for raw SPI operation. Do not equate acknowledged
SPI bytes with visible OLED acceptance.

The [AAI13 report](../../docs/reports/wifi-static-memory/README.md) describes the
release memory measurements and Wi-Fi exception. Earlier recordings describe
their original image identities; repeat the relevant checks for a changed image
or network. The [hardware procedure](../../HARDWARE_ACCEPTANCE.md) covers broader
failure, persistence, electrical and optional peripheral acceptance.

## Periodic I²C register reads

The professor-feedback change starts with one bounded generic I2C operation:
write a one-byte register pointer, read 1–16 bytes, and publish a selected
contiguous part of that response as raw bytes. The register, read length, byte
selection, optional 0–10 ms read-address pause, sample period and timeout come
from JSON. This reuses one firmware driver across compatible register-reading
devices; it does not generate C, add arbitrary writes or run a script.
Generic SPI/UART recipes and configurable onboard ML are still separate work.

The existing Profile → Review & apply → Live monitor flow and visual style
remain. A recipe draft uses an existing I2C interface and an explicitly confirmed
address; it never probes pins, rewires a bus or applies itself. The builder is
additive and rejects an address already owned by any enabled or disabled device.
Use the complete `i2c-recipe-demo` example to replace the BMP388-specific reader
while keeping the current OLED/radar wiring. Its raw register values are not
compensated temperature or pressure readings. The OLED keeps radar/health sources.

Success means two reviewed JSON register reads run on the same firmware image,
the monitor shows the returned bytes with freshness, invalid ranges/duplicate
addresses are rejected without changing the active plan, and ordinary BMP/OLED/
radar profiles still work. Host tests and a firmware build establish software
checks only; dated board captures are needed for each physical claim.

The register-read form prepares one reader on an I2C interface already present in the
draft. Connect firmware advertising `i2c_recipe`, select the bus and address,
review the register/read/selection fields, confirm the physical operation, then
prepare the draft. Preparing does not stop, apply or save anything. It clears
the previous review, so review the resulting whole profile before Apply.
Adding a recipe at the BMP388 reader's address is rejected even if that reader
is disabled; the complete example intentionally replaces that owner.

`POST /api/recipe-prepare` requires `profile` (JSON text), `recipe` (the
strict [draft schema](../../schemas/i2c-recipe.schema.json)) and
`wiring_confirmed: true`. The only optional key, `replace_device_id`, explicitly
edits an existing `i2c_recipe` with that same device name. Editing preserves its
task, enabled state, output queue and unrelated devices; it cannot replace a
BMP388-specific driver or rename a recipe. Its response contains the copied `profile`,
a readable `summary` and a draft-only `notice`. `/api/catalogue` supplies
`recipe_limits` and `recipe_examples` independently of connected capabilities.
The same host planner validates imported/manual profiles; the firmware admits
them independently. Raw observations use telemetry version 5, type 6:
`value.bytes` contains 1–16 selected bytes with no inferred units. Older typed
telemetry versions are retained. Fault/missing samples expose no value.
Raw recipes currently use the USB observation path; the existing network frame
encoder rejects this new raw sample type. No MQTT/BLE raw-reading support or
wireless acceptance is claimed by this change.

`workflow.py` owns connection state, confirmations and final admission checks.
`drafts.py` constructs catalogue instances and output queues without serial I/O;
`discovery.py` adds observation-specific recommendations and connection policy.
Both discovery and manually selected I2C drafts use the same instance builder.
Their wiring policies remain explicit: discovery requires its reviewed preset,
while manual selection reuses a matching bus's existing speed.

Python capture callers use `Workflow.connect(port=...)`, `review(profile=...)`
and `apply(review_id=...)`. Each operation owns the same nonblocking lock,
connection-error cleanup and single-use review checks as the HTTP entry point.
`contracts.py` defines their JSON-compatible result fields; recorded receipts
retain their existing shape. `dispatch` decodes browser actions only.

1. In **Profile**, choose an example or import JSON. Use the JSON editor in **Review & apply**
   for custom instances, wiring, timing and routes. Optional discovery is under
   **Add a device**; interfaces and queues are under
   **Wiring & resources**.
2. Select **Validate and review** to open **Review & apply** with the requested
   resources, devices and rules. Errors remain visible above all three views.
3. For deployment, explicitly connect to the application firmware's serial port,
   then review again against its reported compiled capabilities.
4. **Apply & save** replaces the whole plan and attempts persistent storage.
   Check both the result and active/saved revisions. Retry saving is a separate,
   explicit operation; it does not reapply the profile.
5. **Live monitor** shows source identity, freshness and health.
   Disconnecting leaves the board running. Recover retries cleanup only when
   recovery is required. To stop a healthy setup, review/apply an empty hardware
   profile. **Apply & save also replaces the saved startup profile**; keep the
   original profile file if you intend to restore it later.

Switching views preserves the draft and current review; editing the draft still
requires a new review. The view tabs support Left/Right arrows and Home/End.

Discovery requires compatible application firmware; check its reported feature
bits before use. Combined peripheral, radio and persistence acceptance remains
separate from the working USB workflow. Offline review
uses the host reference catalogue; it is not permission to wire or deploy a
profile. Connected review enforces the narrower compiled target limits.
The original first-prototype profile still uses unsupported GPIO2 CS.

## Device discovery

Use the existing discovery card, then **Add to profile → Validate and review →
Apply & save**. No suggestion automatically deploys or replaces existing entries.

| Choice | What happens |
|---|---|
| I2C | Confirm wiring, then Detect: approved chip-ID reads at 0x76/0x77. Only an unambiguous supported match can be added. |
| Radar | Receive-only UART1 RX20/TX21, 256000 8N1 for one second. Three clean complete reports yield a candidate; confirm the model. Zero-target reports count. |
| OLED | Prepare manual selection, confirm module/wiring and choose an existing compatible readings queue. No sensor or fake readings are added. |
| HC-SR04 | Explicit manual model/wiring confirmation, including the Echo voltage divider. |
| PWM | Explicit manual confirmation adds a disabled GPIO1 output. Schema 2.1 is required; review a rule before enabling it. |

I2C/UART observation requires an empty hardware configuration. Detection never
stops a working driver or applies EMPTY for you. Manual selection reads only
USB state/capabilities, not peripheral buses. New firmware is needed for radar
observation (feature bit value 4); manual choices need the compiled driver/schema.

Settings show their catalogue, preset or observation source. BMP bench defaults
are 500 ms/100 ms timeout; priorities/stacks come from descriptors and queue
depth from the draft. These are deterministic settings, not AI recommendations.
The OLED preset is the tested six-pin SPI layout: reserve GPIO4 unwired, use no
other device on that SPI bus. It conflicts with HC-SR04's GPIO4; PWM1 conflicts
with I2C SCL1. Compatible interfaces are reused; other conflicts/duplicates are
rejected without changing the draft or silently upgrading its schema.

Tokens are single-use, bound to the draft, preset, USB session and board state,
and expire after 60 s. Confirmation rotates a token without extending expiry.
Edits, reviews, reconnects, new detection, apply/save/recovery, failed snapshots
and observed uptime regression invalidate evidence. State/capabilities are
rechecked before adding. A nonce/build hash is not a boot ID: an unnoticed reset
may escape detection. Reconnect and detect again after resetting or rewiring.

Manual confirmation is user testimony; radar framing is model compatibility,
not exact identity. Silence, noise, ambiguous matches and unavailable drivers
cannot become measured proof. Physical discovery, OLED visual confirmation and
GPIO acceptance remain separate checks. See [discovery design](../../docs/README.md).

## Deployment contract

`Workflow` owns one port and one reviewed candidate. It calls the shared planner
and existing USB client. No USB access happens until Connect; DTR/RTS remain
deasserted and there is no reset/flash path. The serial port must not be held by
another monitor. The UI never automatically retries an uncertain mutation.
After a timeout/disconnect, reconnect and query active/saved revisions before
deciding what to do next.

Review retains exact compact profile bytes and the observed board state. Apply
consumes a single-use review token, then rechecks state and capabilities. Edits,
reconnects, save/recovery requests and a replaced review invalidate it. Firmware
independently admits every submitted profile. JSON numbers must be integers;
compact profiles are bounded to 8192 bytes.

The server binds only `127.0.0.1`, checks Host/Origin and a per-server session
token, bounds request bodies, and serves an explicit allowlist of UI assets. It is a local
single-controller tool, not a network service or multi-user deployment system.
POST framing requires one decimal Content-Length of 1–65,536 bytes and rejects
transfer encoding. Within the existing three-second socket timeout, bounded
request bodies are consumed before returning an access rejection; this prevents
unread TCP bytes from hiding the 403 response behind a Windows connection reset.
Unauthorized bodies are never parsed or dispatched. Malformed/oversized framing
is rejected before reading payload data. This changes no UI or review/Apply rule.
The browser polls one snapshot per second while visible, with one outstanding
operation. A failed snapshot clears current readings. Health counters are
historical; no actual PWM duty or physical feedback is inferred from them.

## Read-only capabilities

Wire command 7 (`CAPABILITIES`, response `0x87`) has zero revision/payload and
uses the existing CRC frame. Like telemetry, it cannot consume mutation IDs
or evict an exact-retry response. Payload words are little-endian uint32:

| Offset | Meaning |
|---|---|
| 0..28 | version=1, hardware-qualified, max profile bytes, driver count, ordinary GPIO mask, max instances, max workers, feature bits |
| 32 | Board variant in 32 zero-padded ASCII bytes |
| 64 | Maximum node schema in 32 zero-padded ASCII bytes |
| 96 onward | Zero-padded 32-byte driver IDs; at most eight |

Length is `96 + 32 * count`, at most 352 bytes. Feature bit 0 means bounded rule
routing; bit 1 means explicit reviewed I2C discovery (D2); bit 2 means the
reviewed receive-only UART observer (D4). Other bits are reserved. The decoder
rejects unknown bits. Manual draft selection does not need a discovery bit.
Schema 2.1 also accepts schema 2.0 sensor/display
profiles. UART20/21 follow the board's fixed UART contract, separately from the
ordinary GPIO mask. Compiled support is shown separately from hardware
qualification, which remains **false**. `configs/builds/target-pending.json`
remains empty.

## Source and verification

Member 5's stdlib server/workflow came from cached `system-integration`
`bab09246a477dbedef36a55dad7cd319bac1c1ab` (CharmTzy). Appearance came from
`6a16f2473a348fda0d95eedf12c3839b0010f687`: cards, icons, Roboto fonts, responsive
layout and themes. The current app uses C3 schemas and the real USB client;
device tiles are draft data, live pills are connected observations. Font notices
remain in [assets/fonts/README.md](static/assets/fonts/README.md).

```powershell
python -B -m unittest discover -s tests/target -p test_configurator.py -v
python -B -m unittest discover -s tests/target -p "test_recipe*.py" -v
python -B -m unittest discover -s tests/target -p test_discovery_ui.py -v
python -B -m unittest discover -s tests/target -p test_discovery_confirm.py -v
python -B -m unittest discover -s tests/discovery -v
node --test tests/target/test_dashboard.mjs
```

Checks cover review-token expiry, additive drafts, state changes, conflicts,
uncertain deployment, HTTP boundaries, C/Python parity and presentation. Fake-board
Edge walkthroughs cover themes and desktop/mobile flows. These are not physical
device detection. Repeat the checks for the source being changed.

`npm run test:browser` exercises the actual HTML and ES modules with local HTTP
fixtures. It covers review/discovery races, delayed imports and catalogue loading,
HTTP decoding failures, telemetry clearing, and the catalogue draft workflow.
Local import/format/download failures preserve
board review evidence; failed board requests invalidate it. Datasheet matching
returns server-owned `draft_builder` requirements (eligibility, addresses and
enable-pin requirement), so the form does not depend on startup catalogue timing.
Preparing a draft rechecks eligibility, firmware, wiring and the complete profile.
