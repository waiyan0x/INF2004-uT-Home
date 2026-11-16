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
objects; keep using the configuration tools for setup and recovery.

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
hardware. Markdown compilation uses reviewed I2C register reads, a 100 kHz bus
and a 100 ms timeout. Description schema 1.0.0 is independent of wire version 2. Compilation now emits
version-2 transaction JSON; the target limits device names to 15 characters and
rejects longer names rather than truncating. The description retains its
1-16-byte read limit;
the general `TJ` service independently allows up to 32 received bytes.

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
also verifies entity states in a real Home Assistant instance. **A running Home
Assistant instance and a live ESP have not yet completed acceptance together
with this adapter.** The existing Configuration
Desk has no Runtime Transactions view yet; its UI needs review before changes.

For live acceptance, first confirm the board/image, idle plan and reviewed
BMP388 wiring as described above. Start the session with the display option,
then use the same explicit chip-ID / raw-pressure / chip-ID sequence. Check that
HA shows rule and count changes, that `load` alone leaves raw bytes unavailable,
and that the values expire after execution stops. Restart HA and the broker,
then close/reopen the same host journal; verify no new execution request is sent
by those recovery actions. Stop the host and confirm unavailable entities.
Record actual HA version, firmware identity and observed results separately
from the software checks before claiming that the full display path is accepted.

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
ESP/HA acceptance remains open, and TLS implementation work stays paused.

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
The full backup and original application remain private. The user reiterated
that TLS work is intentionally paused to focus on HA; do not reopen TLS
implementation as part of HA-only work. The timeout cause and unattended Apply
reset remain unestablished. Keep readiness/freshness checks intact and physical
ESP/HA acceptance open. The old VM instability remains a separate unresolved
issue; the tested HA Container and simulated display path remain available.

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
