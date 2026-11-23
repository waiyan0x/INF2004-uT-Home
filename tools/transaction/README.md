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
that MQTT is responding. The current live trial below encountered repeated
status timeouts at this boundary; do not bypass readiness or replay an uncertain
command to continue a demonstration.

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

Host tests verify the messages through a real local MQTT broker and compiled C
owner with simulated buses. The [isolated HA check](#home-assistant-vm-check)
also verifies entity states in a real Home Assistant instance. The [physical chip-ID check](#physical-bmp388-chip-id-in-home-assistant)
verified one real ESP execution through this adapter in Home Assistant Container. The approved Configuration Desk Peripheral transactions tab is
implemented. Its HTTP actions passed the local compiled-owner/MQTT/HA A/B/A,
expiry and journal-reopen check on 17 November, with exactly three simulated
operations and none added by loading or recovery. This supplements the existing
HA checks; it is not physical ESP acceptance.
The subsequent [operator walkthrough](../configurator/README.md#simulated-operator-walkthrough)
was user-confirmed through the Desk controls, including preview/active separation,
all three results, expiry and disconnection with a responsive HA page.

For live acceptance, first confirm the board/image, idle plan and reviewed
BMP388 wiring as described above. Start the session with the display option,
then use the same explicit chip-ID / raw-pressure / chip-ID sequence. Check that
HA shows rule and count changes, that `load` alone leaves raw bytes unavailable,
and that the values expire after execution stops. Restart HA and the broker,
then close/reopen the same host journal; verify no new execution request is sent
by those recovery actions. Stop the host and confirm unavailable entities.
Record actual HA version, firmware identity and observed results separately
from the software checks before claiming that the full display path is accepted.

### Physical BMP388 chip ID in Home Assistant

On 19 November 2026 the ESP32-C3 read register `0x00` from the physical BMP388
at I2C address `0x77`. Home Assistant Container 2026.11.1 recorded the raw hex
byte `50` at 22:15:15 Singapore time. Firmware commit `370d09e` used the
repository-default TLS configuration. The controller sent commands to the bench
TLS broker and its host display adapter published to the separate WSL TLS broker
using the physical node identity.

| Check | Observation |
|---|---|
| Idle readiness | Active and saved idle profiles matched. A fresh MQTT reply reported generation 2 on the original boot. |
| Load | HA showed the acknowledged BMP388 rule at revision 1. Raw bytes and counts stayed unavailable. |
| Execute | The broker recorded one update and one execution. HA showed `execute_ok`, `i2c`, raw `50`, TX 1 and RX 1. Its request ID, boot ID, generation and completion time matched the ESP reply. |
| Expiry and disconnect | After five seconds HA showed `result_expired` and cleared the bytes and counts. All six entities became unavailable when the publisher disconnected. |

The temporary firmware installation ended with a verified full 4 MiB flash and
profile restore. The temporary hotspot and firewall rule were removed and the
WSL broker ACL matched its original hash. The chip-ID result remains in HA
history. Local evidence is in `build/ha-bmp388-physical-20261119/verified-result.json`
and `ha-history.json` in the same folder.

The subsequent [physical Desk walkthrough](../configurator/README.md#physical-bmp388-walkthrough)
verified chip ID, raw pressure and chip ID through the browser controls.
The [physical restart check](../configurator/README.md#physical-restart-recovery)
covered the Desk, both brokers, HA Core and the ESP. Repeatability of idle
MQTT readiness remains open. The earlier VM observations below refer to HA OS.

### Current live trial

The 15 November 2026 trial used host implementation `be626550` and the connected
ESP32-C3/BMP388 on the existing installed firmware. USB capabilities,
authenticated node/boot identity and saved-profile CRC were checked; the flash
image hash was not reread and no firmware was flashed.

A real MQTT observer received all six discovery definitions and the adapter's
ready-state message from a live ESP status snapshot. This did **not** complete
the A/B/A result-display check: status queries timed out after applying the idle
profile, including a load's readiness query before any update was published.
After a physical reconnect, five status queries passed on the normal profile;
another idle-plan attempt then failed to establish readiness. The cause remains
unisolated. Keep this separate from the earlier successful controller-only
[hardware run](../../firmware/src/network/README.md#live-mqtt-check).

Home Assistant OS 18.4 / Core 2026.11.1 was installed locally and its MQTT
integration accepted the private broker settings. VirtualBox repeatedly stalled
and HA API reads timed out; a VMware comparison failed to start with a VM runtime
access violation. Neither entity rendering nor HA restart/expiry acceptance is
claimed. Both test VMs are stopped, with their disks and private account retained.

Every attempted idle profile was explicitly restored. The final active/saved
revision is 941, CRC 3623560383; BMP388/radar samples were fresh and all three
workers reported transfers without errors/timeouts in the cleanup snapshot.
The temporary broker is stopped. Local-only setup, unsuccessful attempts and
cleanup evidence are under `build/homeassistant-live-20261115/`, especially
`setup.json`, `cold-start-status-probes.json`, `mqtt-display-results*.jsonl` and
`final-board.json`. VM disks, credentials, tokens and logs must stay ignored.

### Home Assistant VM check

The 16 November follow-up isolates Home Assistant from the physical ESP and
hotspot. HA OS 18.4 / Core 2026.11.1 runs in the existing local VirtualBox VM.
An authenticated, certificate/name-verified TLS broker on host loopback serves
the synthetic node `uthome-0123456789ab`; the existing compiled C owner uses
simulated buses. No board was contacted, reset or flashed during this check.

HA's authenticated state API verified all six discovered sensors, chip-ID /
raw-pressure / chip-ID results (`50`, `00 00 00`, `50`), matching counts and
configured/result attributes, and no raw result from loading alone. Results
expired, and pausing publication for three seconds made all six entities
unavailable. A Home Assistant Core restart and host-journal reopen recovered
without increasing the three simulated operations; closing the display made
all six entities unavailable. This verifies HA entity behavior, not physical
sensor data or browser rendering. Browser automation was unavailable.

The original VM configuration reproduced guest kernel RCU stalls, watchdog
errors and VirtualBox heartbeat gaps up to 110 seconds during the restart
trial. Disabling paravirtualization stalled earlier in boot. Restoring the
default KVM paravirtualization and enabling the VM's high-precision event timer
(`VBoxManage modifyvm uT-Home-HA-demo --hpet on`) allowed the full check to pass.
After the Core restart, all 30 API probes over five minutes passed in 8–53 ms.
VirtualBox then logged 97- and 252-second heartbeat gaps while shutting down,
after shorter 8–10-second gaps. **HPET did not establish a stable VM.** The
shutdown did not complete; the VM was powered off and its latest disk changes
preserved. No successful final clean restart is claimed. The underlying
host/hypervisor cause is not established. See the
[VirtualBox timer option](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/vboxmanage.html)
and the earlier [HA timing reports](https://github.com/home-assistant/operating-system/discussions/2292).
Windows Hyper-V, Memory Integrity and firewall settings were not changed.

The local web address remains `http://127.0.0.1:18123` (forwarded to this guest's
configured port 80). The VM has 4 GB RAM, two CPUs and EFI. Guest loopback access
was enabled for this isolated test; HA's MQTT integration now uses `10.0.2.2`
port `18883`, a private fixture CA and account. These are test settings, not the
physical ESP broker configuration. The original setup, a clean pre-test VM
snapshot and copies of the stalled guest's disk changes are preserved locally.

Evidence and private helpers are under `build/homeassistant-20261116/`, notably
`acceptance3.jsonl`, `acceptance4.jsonl`, `stalled-kvm-final-vbox.log` and
`hpet-startup.jsonl` / `hpet-responsiveness.jsonl`. Earlier failed harness attempts
are retained. The private fixture was corrected to advance device time in real
milliseconds; maintained
firmware, host code, tests and dependencies were not changed. Use a fresh
temporary broker and a new run label when repeating the isolated check. Keep
credentials, VM disks and raw logs ignored. The VM and temporary Windows broker
are now stopped. The next approved runtime is HA Container in WSL2 below.

### Home Assistant Container in WSL2

After the user's Windows restart on 16 November, the dedicated `uT-Home-HA`
WSL2 distribution imported and started successfully. It runs Ubuntu 24.04.5,
WSL 3.0.5 / kernel 6.18.58.1-1, Docker Engine 29.10.0 and Compose 5.7.1. The
Ubuntu image checksum was verified against Microsoft's distribution catalogue;
Docker was installed from its signed Ubuntu repository. HA is pinned to
`2026.11.1`, Mosquitto to `2.1.2-alpine`; actual image digests are recorded locally.

From the repository root, start the local demonstration with:

```powershell
.\build\homeassistant-wsl-20261116\start-ha.ps1
```

Open `http://127.0.0.1:18123`. Login details are in the local-only
`build/homeassistant-wsl-20261116/private/ha-access.json`; keep that file private.
HA uses the `Asia/Singapore` timezone. Run the start helper again after a Windows
restart. It creates one hidden WSL keepalive process, guarded by a Linux file
lock, then starts the containers. The initial trial showed that WSL exited after
the installer finished, stopping HA and MQTT. This matches Microsoft's guidance
that [systemd services do not keep WSL alive](https://learn.microsoft.com/en-us/windows/wsl/systemd#how-does-enabling-systemd-affect-wsl-architecture).
No Windows login task or global WSL settings were added.

To stop cleanly:

```powershell
.\build\homeassistant-wsl-20261116\stop-ha.ps1
```

The helper stops both containers before terminating only `uT-Home-HA`. HA has
a 120-second stop grace period. Persistent data lives at `/opt/uthome-ha` inside
the Linux filesystem, whose disk is under the ignored setup folder. Preserve
that disk and the private configuration backup. Do not rerun the import or
installation scripts as a routine start command.

Both published ports bind to localhost: `18123` for HA and `18883` for TLS MQTT.
HA connects to Docker hostname `mqtt:18883`, using a custom CA and the scoped
`ha-test` account with certificate/name verification. Windows TLS access was
verified too. No privileged container or hardware passthrough was needed.
The broker ACLs and credentials serve only synthetic node `uthome-0123456789ab`;
they are not the physical ESP broker configuration. Its ACL/password/key files
are owned by the container's MQTT user with restricted permissions.

The real-HA API check passed discovery of all six sensors, chip-ID / raw-pressure /
chip-ID results (`50`, `00 00 00`, `50`), matching counts, load-without-execution,
result expiry, publication-pause expiry, HA Core restart, MQTT restart and host
journal reopen. Exactly three simulated operations occurred; recovery added none.
Core restart recovered in about six seconds. Closing the fixture made all six
entities unavailable. They remain unavailable while the fixture is stopped.
These observations use the existing C owner/HAL with simulated buses, not a
physical ESP or sensor.

Both containers subsequently stopped with exit code 0 and no OOM kill. The
dedicated WSL environment stopped, a configuration backup was taken, and the
start helper recovered HA with the saved settings and six unavailable entities.
All 30 subsequent API probes over five minutes passed in 3–25 ms. Final checks
found both containers running with no Docker restarts or failed systemd units.
The user confirmed that the dashboard loads normally. Automated browser control
stopped because its tool could not enforce URL policy for the Windows browser;
rendered sensor details were not inspected automatically. The bounded API check
does not establish unattended long-term reliability or physical ESP acceptance.

Local-only evidence and helpers are under `build/homeassistant-wsl-20261116/`:

- `preparation.json`, installer/import logs and `ubuntu-download.json` retain
  preparation evidence; `runtime-verification.json` records the subsequent run.
- `acceptance1.jsonl`, `clean-stop-state.json`, `stopped-state.txt`,
  `cold-start.json` and `post-cold-start.jsonl` record acceptance and recovery.
- `compose.yaml`, `install-container.sh`, `start-ha.ps1`, `stop-ha.ps1`,
  `mosquitto.conf` and `mqtt-acl` describe the local setup.
- `private/ha-config-backup-20261116.tar.gz` holds the stopped configuration;
  credentials, tokens, certificates and keys remain under `private/`.

Keep these artifacts ignored. Use a fresh fixture journal for each run and the
dedicated test broker for this local setup. The earlier refused localhost
connection was caused by the observed WSL shutdown; a later PowerShell helper failure came from treating
Docker progress on stderr as a terminating error. Both attempts are retained.
The helpers now request quiet progress and check command exit codes.

Installation references: [WSL](https://learn.microsoft.com/en-us/windows/wsl/install),
[Docker Engine](https://docs.docker.com/engine/install/ubuntu/) and
[HA Container](https://www.home-assistant.io/installation/linux#install-home-assistant-container).

#### Repeatable simulated display demo

On 17 November, the user visually confirmed all six sensors on the existing HA
device page through chip-ID / raw-pressure / chip-ID switching, result expiry
and display disconnection. The authenticated API independently matched every
phase. Loading alone cleared raw/count values without executing a simulated
operation. This completes the simulated display walkthrough; combined physical
ESP/HA acceptance remains open. TLS work resumed afterward on 17 November.

With the containers running, open **Settings > Devices & services > Devices**
in HA and select **uT-Home 0123456789ab**. From this checkout's root, run:

```powershell
.\build\homeassistant-wsl-20261116\run-ha-demo.ps1 -Auto
```

The local helper uses the existing compiled C owner/HAL, simulated buses and
scoped test MQTT account. It creates a new journal and `events.jsonl` under
`build/homeassistant-wsl-20261116/visual-YYYYMMDD-HHMMSS/`. Run only one fixture
for this synthetic node. The automatic sequence lasts about one minute and
stops its fixture afterward; HA and its broker remain running.

| Phase | Active rule | Status | Raw bytes | TX / RX |
|---|---|---|---|---|
| Chip ID | `bmp388 / revision 1` | `execute_ok` | `50` | `1 / 1` |
| Raw pressure | `bmp388 / revision 2` | `execute_ok` | `00 00 00` | `1 / 3` |
| Chip ID again | `bmp388 / revision 3` | `execute_ok` | `50` | `1 / 1` |
| Stop reads | Revision 3 remains | `result_expired` | Unavailable | Unavailable |
| Disconnect display | Unavailable | Unavailable | Unavailable | Unavailable |

Protocol stays `i2c` until display disconnection. In automatic mode, each data
phase lasts about 12 seconds. Both modes execute a simulated read every two seconds
to keep results fresh. Brief unavailable transitions during each new request
are expected. Stopping reads allows the ordinary five-second freshness limit
to expire; the helper does not change that limit or fabricate result timestamps.

For a manual walkthrough, omit `-Auto`. The helper starts with chip ID and waits
for phase changes. In a second PowerShell window at this checkout's root, use:

```powershell
Set-Content .\build\homeassistant-wsl-20261116\visual-phase.txt -Value 'pressure_raw' -Encoding utf8
```

Then use `chip_id_again`, `expiry`, `offline`, and `stop` as the value, one at a
time after checking the display. Restart the helper to leave the offline phase.
Manual mode stops after 15 minutes even without `stop`. The visual walkthrough
used 48 simulated reads to hold results for observation; expiry/disconnection
added none. Its user observations are recorded separately from API evidence in
the private run folder. The earlier three-operation restart acceptance remains
a separate result. These private helpers are local setup artifacts, not a new
Configuration Desk UI or firmware change.

### MQTT status investigation

The 16 November follow-up used the user-confirmed unchanged bench, checked the
current USB port/capabilities and preserved profile CRC, and sent only read-only
`TJ` status queries. No rule update/execution, firmware flash or Home Assistant
trial occurred. The installed flash hash was not reread. These observations
extend the earlier trial; they do not establish its root cause.

| Live observation | Result |
|---|---|
| Initial normal-profile controls | Two sets of five status queries passed |
| First idle Apply | Reply timed out; boot identity changed without user intervention; original profile remained saved/active |
| Normal profile after that reset | Five queries timed out; another five timed out after a temporary broker restart; USB/HTTPS remained responsive |
| Second idle Apply | Succeeded; 3/8 status queries passed, followed by 0/5 after a quiet period |
| Restored normal profile | Final 5/5 status queries passed; a separate preceding ping check lost 7/10 replies |

Each MQTT probe had an eight-second deadline. Broker packet-event timestamps
show resubscription about 34 seconds after the successful idle Apply. The first
four queries had no ESP subscription to receive them. One later query was
forwarded but its reply arrived about 11 seconds later; another forwarded query
after the quiet period had no observed reply, followed by disconnection. The
broker received and immediately answered the observed keepalive requests.
Successful idle replies reported the same boot, generation 2 and readiness.
Thus reconnect delay explains some timeouts; it does not explain all subsequent
loss or the unattended reset. Ping loss on the normal plan is a broader network
symptom, not proof of an RF, power, SDK or host cause. Two scoped Windows packet
captures produced no packet events even while broker events were observed, so
they cannot establish where packets were delayed or lost. The reset was not
captured in a raw fault log; the later captured Apply did not reset.

Software checks now exercise the actual C MQTT adapter with all samples absent,
a generation change, resubscription, two keepalives and copied query/reply
traffic. A separate compiled-C case verifies that recent failures retain the
bounded 30-second backoff across a plan change, then resubscribe successfully.
All 76 affected tests and the full pinned 340-file C gate passed. These tests
use transport doubles and do not reproduce the live failure. No C source,
generated C template, timeout policy or target image changed.

The final active/saved profile is revision 943, CRC 3623560383, with fresh
BMP388/radar samples and zero reported errors/timeouts on all three workers.
The broker and captures were stopped, capture filters removed and the laptop's
original network restored. Local-only evidence is under
`build/mqtt-idle-20261116/`: the probe JSONL files, broker event logs,
`post-restore-ping.txt`, `final-board.json` and software-check logs. Preserve the
unsuccessful attempts alongside the successful control.

#### Normal-profile packet capture

The subsequent check, after HA Container acceptance, again used the user's
confirmed unchanged bench. It kept active/saved profile 943 and CRC 3623560383
throughout: no Apply, rule load/execution, reset or flash. All 20 MQTT status
queries replied within their eight-second deadline, but took 0.152–6.982 seconds.
Boot identity and generation stayed unchanged. USB continued to report Wi-Fi
online and zero errors/timeouts on all three peripheral workers.

A bounded Windows IP-level capture filtered to the ESP produced usable TCP
headers. For the 6.982-second query, the broker forwarded the request in
0.333 ms. Its TCP data appeared six times, with distinct IPv4 IDs, before the
first observed advancing acknowledgment 6.791 seconds later. The result arrived
about 3 ms after that acknowledgment. Repeated data also appeared in the other
direction. This establishes retransmission/delayed acknowledgment on the ESP
path; the host-side capture cannot locate the fault at the ESP, radio, hotspot
or host interface. It does not establish a firmware fix.

| Control | Observation |
|---|---|
| Local authenticated broker keepalives | Eight replies in 0.569–1.435 ms |
| Laptop to actual hotspot gateway | 12/12 echo replies in 2–5 ms |
| Laptop to ESP, one-second ping deadline | 7/20 echo replies |
| Separate ESP check, three-second deadline | No ESP echo replies; eight local unreachable errors and four timeouts |
| Phone moved to clear space 0.5–1 m from ESP | 9/20 ESP echo replies; gateway 12/12; five status replies in 0.188–3.305 seconds |

The closer-phone capture still showed TCP retransmissions; placement did not
clear the problem. These small sequential samples do not establish an RF cause
or a reliable improvement. Windows counts local unreachable errors as received
packets, so its summary for the three-second check is not eight ESP replies.

Two PktMon attempts recorded counters but no packet events; their empty captures
cannot locate loss. The later raw-IP captures contain 156 and 205 board packets
and closed their sockets afterward. A local dependency-access error prevented
one comparison attempt from publishing any MQTT query; it is retained separately
from the successful rerun. An initial gateway probe used an assumed, incorrect
address and is excluded from the gateway comparison.

Evidence is local-only under `build/mqtt-followup-20261116/`, including
`timing-analysis.json`, `phone-near/timing-analysis.json`, broker/packet logs and
`normal-final.json`. The temporary broker stopped, capture filters were removed,
and an explicit Wi-Fi rescan restored the laptop's original network and removed
only the temporary bench profile. The physical board remains unchanged.

The approved comparison used a diagnostic with Wi-Fi modem sleep disabled and
a restored-original control to separate a setting effect from a reboot effect.
The installed build's 257 staged source hashes match
the private `ps-none-source` snapshot before its diagnostic changes. This is a
hypothesis: the project leaves power saving at the SDK default, and
[Espressif documents](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/wifi-driver/wifi-performance-and-power-save.html#esp32-c3-wi-fi-power-saving-mode)
that disabling modem sleep trades higher power consumption for lower receive
latency. It does not explain the measured loss by itself. The private
diagnostic now passes all 64 affected tests with warnings as errors, the full
329-file pinned C gate, manual C review and the real-target build. Snapshot and
artifact hashes verify; only `c3wsta.c` and its header differ among the staged
target sources. SDK configuration, generated private setup and partition bytes
match the installed build. Fixed DRAM remains 260,386/278,528 bytes, with identical
per-module measurements/limits and aggregate caps. The two budget files have
different hashes; their historical hashes are preserved.
The build is `C:/Users/kkak4/esp/uthome-ps-none-20261116`,
with image SHA-256
`b57068ec0b5aad969adf1c24956a4568100774f632b0291d01293badeced5333`.
The local `diagnostic-verification.json`, `diagnostic-review.json` and
`firmware-trial-plan.json` specify the checks and backed-up, app-only A/B/A trial.
The user authorized the temporary app-only write, full verified backup and
restoration. The original installed application matched its recorded image hash.
Each write was verified; after restoring the exact original 1 MiB application
partition at `0x10000`, the entire 4 MiB flash matched the pre-trial backup.
No Apply, rule load/execution or wiring change occurred.

| Firmware control | ESP pings | Initial status queries |
|---|---|---|
| Restarted original | 20/20; 43–240 ms, mean 134 ms | 0/10 replies |
| Modem sleep disabled | 20/20; 4–26 ms, mean 10 ms | Seven timeouts, then three replies |
| Restored original | 20/20; 4–246 ms, mean 119 ms | Five timeouts, then five replies |

Once the diagnostic connected, five additional queries passed in 23–201 ms.
The restored original's successful replies took 23–234 ms. During captured early
original-firmware attempts, TCP connected promptly but no TLS payload appeared
before closure about 30 seconds later. These are observations for later transport
work, not a diagnosed cause. Disabling modem sleep reduced observed ping latency
but did not eliminate initial MQTT timeouts. The original also recovered; no
production change was promoted from this bounded comparison.

The final board retains profile 943 / CRC 3623560383, with fresh BMP388/radar/health
samples and zero reported errors/timeouts on all three workers. The capture
closed after 556 board packets. The broker logged closure and no port-8883
listener remained; its process wrapper returned 1 and the log retained a warning
about one message not broadcast during shutdown. The laptop's original network
was restored. HA and its isolated broker remained running with zero container
restarts; an authenticated HA API read took about 8 ms. All six simulated entities
were correctly unavailable with their fixture stopped.

Trial evidence is under `build/mqtt-powersave-20261116/`: `verification.json`,
probe logs, `trial-raw.pcap`, final board snapshot and full-restoration verification.
The full backup and original application remain private. TLS work was paused
during this HA-focused comparison and resumed on 17 November. The subsequent
[SDK connect-retry correction](../../firmware/src/network/README.md#mqtt-tls-connect-retry)
passes software checks and a target build. The cumulative comparison below
does not isolate its effect on the earlier live timeouts. The unattended Apply
reset remains unestablished. Keep readiness/freshness checks intact and physical
ESP/HA acceptance open. The old VM instability remains a separate unresolved
issue; the tested HA Container and simulated display path remain available.

#### Backed-up TLS board comparison

On 17 November, the user approved an original/candidate/original comparison on
the confirmed unchanged bench. The candidate came from commit
`385d829e75b80b6f80aac7d7e3c689a836bc0a19`, with application SHA-256
`d645cbddde5b5a650354f5e6fab71035d5d1bc41825e724dc9122869f22358dc`.
It includes the queued TLS storage/retry changes, disabled session tickets and
ESP HTTPS removal. A fresh full 4 MiB backup passed independent flash verification;
the partition layouts matched. Candidate bootloader, partition table, application
and preserved configuration bytes were independently verified before restart.

| Sequential control, normal peripheral profile | Status replies | Observed query time |
|---|---:|---|
| Restarted original | 0/10 | All reached the eight-second deadline |
| Candidate after installation | 10/10 | 0.145–0.891 s |
| Candidate after local broker restart, no board reset | 10/10 | 0.100–0.683 s |
| Candidate pre-Apply readiness check | 0/5 | All reached the eight-second deadline |
| Restored original | 0/10 | All reached the eight-second deadline |

The twenty candidate replies share one boot identity and generation 1. The
later five failures occurred with the normal profile still active. The readiness
guard stopped the idle test before any Apply; no rule load or execution occurred.
The broker logged dispatch of all five requests within 0.528–1.278 ms of receiving
them, but no corresponding result before disconnection. Dispatch is not proof of
delivery to the ESP. Its reader logged a Windows semaphore-timeout socket error;
these observations do not locate the fault in the ESP, radio, hotspot or host.
USB uptime continued from 121.695 to 167.921 seconds through this failure period;
Wi-Fi still reported an address and all three workers reported zero errors/timeouts.

This confirms real encrypted exchanges and one broker reconnection on the
candidate, but sustained reliability and idle-plan acceptance remain open. The
sequential cumulative comparison does not establish which individual change
affected initial connection behavior or diagnose the earlier unattended reset.
Final resource acceptance and the separate old HA VM issue are unchanged.

The original full flash was restored and independently verified against SHA-256
`7e2a57e7ba24af6578ac3d0309254c993ef9490bb7cdbf02f312e032572fb096`.
The final active/saved profile remains revision 943 / CRC 3623560383. Both broker
processes exited successfully, port 8883 closed, and an explicit Wi-Fi rescan
restored the laptop's original network and removed the temporary profile.
Evidence is local-only under `build/tls-bench-20261117/`, including
`trial-results.json`, flash verification logs, USB captures and broker events.
The full backup remains outside the repository. Continue with the later
normal-profile stall before another idle-plan or combined ESP/HA demonstration.

#### USB MQTT observation board trial

The subsequent approved trial used commit
`11dac9bed9c9f15224c87ce2f2836459c5d4fe51`, application SHA-256
`16fd4deaf35d81642efd40f12c6d4da061066feb29b39d493e46ccc0c5bef443`.
This adds copied MQTT state/counters to USB observation. The confirmed unchanged
bench stayed on its normal profile, with a fresh independently verified full
backup before installation. No profile Apply, rule load or execution occurred.

| Sequential control | Status replies | Successful query time |
|---|---:|---|
| Restarted original | 5/5 | 0.213–0.426 s |
| Diagnostic image, five-minute normal window | 241/242 | 0.021–0.413 s |
| Three-minute window containing one broker restart | 140/142 | 0.025–0.415 s |
| Restored original | 5/5 | 0.106–0.416 s |

The first candidate request reached the broker 3.300 seconds before the new
candidate subscription completed; it timed out. All 241 subsequent queries in
that window answered. The restart window had one host connection-loss error and
one timeout, followed by 106 successful queries. Both candidate windows retained
one boot identity and generation 1. USB MQTT failures changed from zero to one
at the controlled restart; replacement/discard counters stayed zero. The three
workers reported zero errors/timeouts at each final observation.

The later normal-profile stall did not recur. Both original-image controls also
passed, so this sequential run does not isolate a firmware improvement or close
the earlier timeout/reset investigation. Frequent USB polling and a short run
limit the result; longer tracing with quiet intervals remains necessary before
idle-plan or combined ESP/HA acceptance.

Windows TCP-header captures covered the active query windows. The normal
capture reached its limit during an approximately 30-second host serial-close
delay; the helper returned 1 because its lifetime check included that cleanup.
This was after the final query and USB observation, not a board/profile guard
failure. The restart-window serial close took about 1 ms. The host delay remains
unexplained; these IP-level captures do not locate a radio fault.

The full original 4 MiB flash was restored and independently verified against
`7e2a57e7ba24af6578ac3d0309254c993ef9490bb7cdbf02f312e032572fb096`.
Active/saved profile 943 and CRC 3623560383 were preserved. Both temporary
brokers and the elevated capture helper exited, port 8883 closed, and the entry
laptop network was restored with the temporary profile removed. HA services
were unchanged; the old VM issue and runtime allocation/peak-memory gates remain
open. Private evidence is under `build/mqtt-observe-bench-20261117/`, including
`trial-results.json`, USB/TCP captures, broker events and flash verification logs.

#### Original-image quiet-interval trace

The next read-only trace on 17 November kept that restored original firmware
and normal profile unchanged. No reset, flash, Apply, rule load or execution
occurred. Twenty short USB sessions, alternating same-thread observations with
a joined observation worker, closed in 0.705–1.447 ms. Process-local tracing of
the installed pySerial Windows calls made no dependency changes.

The longer run then completed these sequential phases:

| Phase | Status replies |
|---|---:|
| Initial readiness | 1/1 |
| Three minutes, USB closed | 149/149 |
| Three minutes, USB polling | 149/149 |
| 90 seconds without status queries, USB closed | No queries |
| Two minutes after the pause | 100/100 |

All **399/399** queries replied in 0.023–0.457 seconds, with one boot identity
and generation 1. Normal sensor telemetry and host keepalive continued during
the pause; this was not an idle peripheral plan. All three serial closes in
the longer run took 0.775–2.676 ms. The 1,573-packet TCP-header capture covered
the whole observer run. Profile 943 / CRC 3623560383 remained active and saved;
the final three workers reported zero errors/timeouts.

Neither the later MQTT stall nor the earlier slow close recurred. This does
not establish a fix or exclude a fault that depends on another condition.
Original firmware exposes Wi-Fi observation v1, without the new MQTT counters.
The next trial should use the checked diagnostic image and test the idle-plan
transition only after reliable normal-profile controls, with a fresh verified
full backup and explicit installation approval.

The broker and capture stopped, port 8883 closed, and the entry laptop network
was restored with the temporary profile removed. HA services were unchanged.
Private evidence is under `build/transport-fix-20261117/`, including
`trial-results.json`, Windows close timings and the retained USB/broker/TCP
records. Remaining TLS allocations, runtime peaks and combined HA acceptance
are still open; the old HA VM instability remains separate.

#### Diagnostic idle-transition trace

A further approved backed-up trial on 17 November used the same checked
`11dac9bed9c9f15224c87ce2f2836459c5d4fe51` diagnostic image identified above.
The unchanged bench completed **398/398** normal-profile status queries across
the same USB/no-USB/90-second-pause sequence. MQTT failure, replacement and
discard counters stayed zero. Five fresh pre-Apply queries also passed.

The single approved idle Apply completed in 64 ms, saving profile 944 /
CRC 2760408070 and advancing generation 1 to 2. All **14** subsequent status
queries timed out during the bounded recovery window. USB remained responsive,
uptime increased continuously, and no unexpected reboot occurred. No idle
quiet/keepalive phase, rule load or peripheral transaction was performed because
idle readiness never arrived.

This trace narrows that failure to connection setup before TLS payloads:

- Three new ESP TCP connections each emitted 13 SYN packets. The first had
  five host SYN-ACKs but no completing ESP ACK in the capture; the other two
  had no captured host SYN-ACK. All 39 complete SYN headers had valid IPv4/TCP
  checksums. None of these three connections carried payload bytes.
- MQTT failures rose to three. Wi-Fi still reported an address initially,
  then reported reason 200 (`WIFI_REASON_BEACON_TIMEOUT`) about 72 seconds
  after Apply and reason 201 (`WIFI_REASON_NO_AP_FOUND`) on subsequent attempts.
  These names were checked against the pinned SDK. Windows also recorded the
  ESP neighbour as unreachable before restoration.
- The operator reported no change to the hotspot phone. This single run does
  not locate the fault in the ESP, hotspot or link, or prove Apply caused it;
  earlier normal-profile stalls remain separate evidence. Windows IP captures
  cannot establish over-the-air delivery. No TLS certificate/record defect or
  firmware fix is established by this observation.

The full original 4 MiB flash was restored and independently verified against
the preserved original hash above; active/saved profile 943 and its original
CRC returned. All three workers reported zero errors/timeouts. The five final
MQTT queries still timed out, despite a reachable neighbour and a later
established TCP connection; that control's cause was not isolated. The two
captures and broker exited successfully, port 8883 closed, and the entry laptop
network was restored with the temporary profile removed. HA services were
unchanged. Private evidence and derived checks are under
`build/mqtt-idle-trace-20261117/trial-results.json`. No owned C changed.

#### Wi-Fi power-save comparison

A private diagnostic based on `53e3ae26c2e66d710d9e9b02c79bb33ec2df5868`
disables Wi-Fi modem sleep after station startup and reports any failure through
the existing startup-error path. This tests a hypothesis from the trace above;
it is not an established fix or a change to the maintained firmware default.
Keeping the radio awake increases power use. The earlier power-save comparison
predated the connect-retry correction and did not test the idle transition.

The isolated patch and checks are under `build/wifi-idle-ps-none-20261117/`.
All **52** affected/USB tests, the full **336-file** pinned C-standard gate,
manual BARR-C review, real ESP32-C3 build and source-snapshot check pass.
Fixed DRAM remains **272,322/278,528 bytes**, with every group cap unchanged.
Only `c3wsta.c/h` differ among target source inputs from the default-power
diagnostic; SDK configuration, TLS/private setup and partition layout match.
The new application SHA-256 is
`c4e84d32d9e3b8c0ee86e7b7efa4f517895d7f318f6ae30e67343bfc86c92d9f`.
Software checks do not establish radio behavior or runtime resource compliance.

The user approved that exact image and the default-power control on 17 November,
confirming the bench was unchanged. A fresh full 4 MiB backup independently
verified against the original hash above. Each image preserved the normal profile,
completed the same normal/quiet sequence and then performed one guarded idle Apply.

| Mode | Normal status replies | Replies after idle quiet interval | Median normal reply time |
|---|---|---|---|
| Radio awake | 465/465 | 10/10 | 27 ms |
| Default power saving | 397/397 | 10/10 | 220 ms |

Both runs also passed five fresh pre-Apply queries. Apply completed in 63/64 ms,
saved profile 944 / CRC 2760408070 and advanced generation to 2. Each first
recovery query timed out, followed by ten ready replies and then the successful
60-second quiet check above. Boot identity and USB uptime stayed continuous;
Wi-Fi reported no disconnect and MQTT failure/replacement/discard counters stayed
zero. Idle retained no workers and reported no apply/service/recovery error.

Each new ESP TCP connection completed with one SYN and one SYN-ACK, about
1.16/1.06 seconds after Apply. The timed-out query was published before that
connection completed, consistent with a brief reconnect gap. The earlier sustained
TCP/beacon-loss failure did not recur. This sequential pair shows lower latency
with the radio awake, but does not establish modem sleep as the fault or justify
promoting the diagnostic as a reliability fix.

The full original flash was restored and independently verified after each image.
The final original control timed out three times, then answered two queries;
its startup delay remains unisolated. Profile 943 and the original CRC returned,
with three workers and zero worker errors/timeouts. All four captures and the
broker exited, the listener closed, and the entry laptop network was restored
with the temporary profile removed. Shutdown warnings are retained in the private
evidence; process/listener cleanup was independently checked. No new transaction
or HA change occurred. Raw captures, invariant checks and file hashes are bound by
`build/wifi-idle-compare-20261117/trial-results.json`. No production power-saving
change was retained. Further diagnosis must distinguish intermittent Wi-Fi/TCP
loss from startup/reconnect gaps; remaining TLS allocation and combined acceptance
work stays open.

#### Laptop-hotspot comparison

No second router or phone is available. A host-only preflight on 17 November
successfully started and stopped a temporary 2.4 GHz WPA2 Windows hotspot.
Neo_5G and the saved hotspot configuration were preserved. The
[per-session API](https://learn.microsoft.com/en-us/uwp/api/windows.networking.networkoperators.networkoperatortetheringsessionaccesspointconfiguration)
avoids replacing the persistent settings. The bench was reconfirmed unchanged;
a fresh USB read found the same board, original NETWORK v1 and profile 943.

The private build under `build/ap-compare-20261117/` uses source
`11dac9bed9c9f15224c87ce2f2836459c5d4fe51`. All **255** target source records
match the default-power control byte for byte. Only private Wi-Fi credentials
and the numeric broker address change; TLS identity, CA, authentication,
transmit power, SDK configuration and partition layout remain identical.
Five provisioning tests, the full **334-file** pinned C gate, manual review,
fresh target build and source-snapshot verification pass. Fixed DRAM remains
**272,322/278,528 bytes**, with every group cap passing. Application SHA-256:
`fad05dade7124d73693a4bcf26edc0c381f1616b9995c30d1800cb98a4263594`.

The user approved that exact image and backed-up trial on 17 November. A fresh
full 4 MiB backup matched the original hash above and independently verified.
The candidate's written regions and unchanged saved-profile region verified
before startup. The ESP joined the laptop hotspot and reported MQTT ready.

Two host setup corrections are retained in the private evidence. The firewall
command needed the native Windows spelling of the same approved executable
path. The first host client attempt rejected the new numeric address because
it was absent from the broker certificate. A process-local address mapping then
used the existing `uthome-bench.local` identity with certificate verification
enabled; no certificate, global DNS setting or firmware change was needed.
The corrected timed controls began about 172 seconds after the ESP boot, so
they do not measure cold-start MQTT latency.

| Normal-profile phase | Status replies |
|---|---:|
| Initial readiness | 1/1 |
| Three minutes, USB closed | 147/147 |
| Three minutes, USB polling | 144/144 |
| Two minutes after a 90-second status pause | 98/98 |

All **390/390** queries replied. Median time was 221 ms; two replies took
1.09 and 4.31 seconds. Both slow queries coincided with repeated host TCP data
at the same sequence number and delayed received ESP traffic. The Windows
IP-level capture cannot locate the loss or delay in the radio, AP, ESP or host.

Five fresh pre-Apply queries passed. The single idle Apply completed in 64 ms,
saved profile 944 / CRC 2760408070 and advanced generation 1 to 2. The first
recovery query timed out, followed by ten ready replies and **11/11** after the
60-second quiet interval. One SYN/SYN-ACK exchange completed the new TCP
connection 1.124 seconds after Apply. The timed-out query reached the broker
before that connection completed, consistent with a brief reconnect gap.
Boot identity and USB uptime stayed continuous; Wi-Fi reported no disconnect,
MQTT failure/replacement/discard counters stayed zero, and idle retained no
workers or apply/service/recovery errors. No rule load or runtime transaction
was sent.

The full original flash was restored and independently verified. Active/saved
profile 943 and the original CRC returned, with three workers and zero worker
errors/timeouts. Restoration was checked through USB; no new original-image
MQTT control was planned. Both captures and the broker exited, port 8883
closed, the exact scoped firewall rule was removed, and the hotspot stopped.
Neo_5G and persistent hotspot settings were preserved. Broker warnings remain
in the evidence; process/listener cleanup was independently verified.

This later run changes both the AP implementation and forwarding path because
the laptop is AP and broker. The earlier sustained TCP/beacon-loss failure did
not recur, but slow TCP traffic remains observable. This does not establish a
phone-hotspot fault or a firmware fix. Remaining TLS allocations, runtime peaks,
combined physical HA acceptance and the separate old VM issue stay open.
No maintained C or HA service changed. Raw evidence, setup corrections,
restoration checks and file hashes are bound by
`build/ap-compare-20261117/trial-results.json`.

#### Plan-change session trial

The [network correction](../../firmware/src/network/README.md#mqtt-plan-changes)
preserves a healthy MQTT session across a plan change when no packet is in
progress. This targets the observed reconnect gap; it does not establish the
cause of the separate intermittent Wi-Fi/TCP failure. Software verification
passes 158 affected tests, the full 334-file C gate and a fresh target build.
Fixed DRAM remains 272,322/278,528 bytes with unchanged caps. Application SHA-256:
`f434c1ade21328feb6e60e78823b687df6fa440bbc63bd2d4b41b64997c9776c`.

The user confirmed the unchanged bench on 18 November. Read-only USB preflight
found the expected board, NETWORK v1 and active/saved profile 943 with its
original CRC. The approved phone-network attempt stopped at host preflight:
the laptop needed its original network for Internet access. The temporary
profile was removed and the original connection verified. No broker, capture,
ESP reset, installation or Apply started. This is not ESP timeout evidence.

The revised candidate uses the temporary laptop hotspot while retaining the
laptop's Internet connection. All **255 target source records** match the
`e402aeed029845f58ab2297faae515e8421d47a3` phone candidate exactly; only private
SSID/password and broker address change. SDK settings, TLS verification and
radio power policy are unchanged. The fresh target/snapshot and fixed-memory
checks, full 334-file C gate and five provisioning tests pass. DRAM is unchanged.
Revised application SHA-256:
`4b12b2cb0098d6e43a0939a05d368da12f90752029024d5a481793c449d9225e`.
The user approved this image and its backed-up trial. The first attempt saved
74 valid normal-profile replies, but its observer incorrectly required the
transaction `ready` flag. That flag means the peripheral plan permits a runtime
transaction; it is correctly false with the normal workers running. The original
failed observer summary is retained, and its assertion was corrected. This was
an observer failure, not 74 MQTT timeouts. The pause request ended that attempt
after full original-flash restoration; its remaining temporary firewall rule
was removed during the resumed host setup.

The resumed trial on 18 November verified a fresh complete 4 MiB backup,
installed the same approved image and verified its unchanged saved profile.
The temporary 2.4 GHz laptop hotspot retained the original Internet connection.
Normal-profile results, using an eight-second client wait, were:

| Phase | Duration | Replies within the wait |
| --- | --- | --- |
| First trace, USB closed | 180 s | 136/136 |
| First trace, USB polling | 180 s | 99/103 |
| First trace, after 90 s without status queries, USB closed | 120 s | 98/98 |
| Read-only repeat, USB polling | 180 s | 147/147 |
| Read-only repeat, USB closed | 180 s | 147/147 |

Including one successful warmup query per trace, totals were **334/338** and
**295/295**. All four timed-out requests reached the broker promptly; their
results arrived after 11.18, 15.52, 24.55 and 33.26 seconds. The repeat's slowest
reply was 0.836 seconds. Both traces kept the same boot identity and generation;
USB snapshots showed MQTT READY, no recorded MQTT failures and an assigned IP.
No TCP SYN/FIN/RST appeared in either capture. The normal-control failure stopped
the idle guard: **no idle Apply, rule load or runtime transaction occurred**.

Offline analysis reconciled all 1,453 first-trace packet headers with the saved
PCAP. Acknowledgements covering the timing-associated request bytes appeared
after 0.94–12.25 seconds. In two return-stream gaps, ESP sequence numbers had
advanced past 178 bytes that the host had not acknowledged; those bytes appeared
later, after gaps of 1.10 and 11.91 seconds. Under the
[TCP sequence/acknowledgement rules](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.4),
this is consistent with missing or delayed TCP data. The encrypted byte-range
association is inferred from timing, not decrypted packet contents. Host capture
does not isolate the radio, driver, network stack or host path, or exclude an
additional application delay. USB polling did not reproduce the fault in the
repeat, so it is not an established cause.

The next normal-profile trace should retain passive full status replies from
the start, alongside broker events and TCP headers. The existing status `now_ms`
can help separate device processing from return delivery. Those timestamps were
not saved for the four late replies; the repeat's passive subscriber saved all
295 replies and published zero requests. The plan-change session-preservation
check remains unqualified until normal controls pass. Do not loosen that guard
or classify the intermittent failure as fixed.

The full original flash was restored and independently verified. USB confirmed
the original active/saved profile and three workers. The immediate postboot
snapshot was still reconnecting; no original-image MQTT acceptance is claimed
on this different AP. Broker, captures and subscriber stopped; both temporary
firewall rules were removed, the hotspot stopped, saved settings were preserved
and the original Internet connection remained active. HA was unchanged.

Local-only evidence is in `build/mqtt-plan-ap-20261118/` and
`build/mqtt-plan-resume-20261118/`. The latter's `trial-results.json` binds the
backup, exact image, observations, restoration and cleanup; the separate
`ack-timing-analysis.json` verifies its 109 saved evidence hashes and records the
packet analysis without rewriting the original results. Raw captures and private
settings remain excluded. The AP changes the forwarding topology, so these runs
cannot establish a phone-hotspot cause. TLS/TCP allocation, runtime resource
peaks and combined physical HA acceptance remain open.

#### Status timestamp capture

The private broker recorder now retains each complete, validated `TJ` status
reply, including replies received after the querying client disconnects. A local
verified-TLS check timed out at 8.002 seconds and still recorded its deliberately
delayed reply at 9.507 seconds, without retrying the request. This verifies the
host recorder, not ESP or radio behavior.

Use USB telemetry `timestamp_ms` to bracket the kernel clock: it and the status
reply's `now_ms` use `tk_get_otm`. `WORKCOST.timestamp_us` instead uses
`esp_timer_get_time` and must not be treated as the same clock. The host records
the start/end of each telemetry request, then compares adjacent clock brackets
with broker request/result times. Status `now_ms` is sampled at owner-loop start,
before USB/network handling; it is not an exact TLS-write timestamp. Report the
observed offset range and loop-timing limitation rather than exact wire latency.

The backed-up follow-up on 18 November reused the same approved application
`4b12b2cb…` and normal saved profile. Its corrected capture answered **393/393**
queries: one warmup, 98 with USB closed and 294 during USB polling, followed by
60 seconds without new queries. Median reply time was 0.221 seconds; maximum
was 0.521 seconds. All 393 full replies were matched to 305 kernel-clock samples;
the USB clock exchanges took 90–183 ms. No timeout recurred, so there is no
late-response classification or demonstrated fix. Added clock sampling changes
the USB observation load and does not qualify the idle-plan correction.

An initial 66/66 observation was retained separately when the clock association
was corrected; the firmware, profile and boot identity stayed unchanged. The
complete original flash was subsequently restored and independently verified,
and USB confirmed the original profile and three workers. Both header captures,
the broker and helpers exited; temporary firewall/AP setup was removed, saved
hotspot settings preserved and the original Internet connection retained. No
idle Apply, runtime transaction or HA change occurred.

Local evidence is in `build/mqtt-status-timing-20261118/`: `trial-results.json`
binds the image, fresh full backup, captures and restoration; `timing-analysis.json`
records the clock ranges and all matched results. The original plan's incorrect
clock description remains preserved with an explicit correction record. Keep
this recorder enabled from the first query in any subsequent diagnostic trace.
Another run without a delayed reply cannot identify the intermittent fault.

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

## Verification and next work

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

The [15 November live MQTT check](../../firmware/src/network/README.md#live-mqtt-check)
used these maintained tools with a real ESP32-C3/BMP388. It verified A/B/A raw
reads, host rejection, host journal reopen, broker recovery and an exact cached
completion. A separate diagnostic owner, after closing the controller, verified
ESP rejection of an oversized read while retaining the active rule. The normal
profile was explicitly restored and checked after restart. No firmware rebuild
or reflash occurred during rule switching. These results cover BMP388 I2C;
runtime SPI/UART, prolonged load and full resource acceptance remain open.

The display tests cover Markdown A/B/A changes, zero bytes, update versus
execution, rejected updates, failed reads, expiry, unknown outcomes and boot/
generation changes. Local-broker tests check retained discovery, non-retained
state, actual offline last will, host reopen, HA birth and broker restart without
another C driver call. They also exercise the public `--home-assistant` option
and show that display failure leaves explicit controller operations usable.
These are MQTT-contract tests, not rendered Home Assistant or hardware acceptance.
