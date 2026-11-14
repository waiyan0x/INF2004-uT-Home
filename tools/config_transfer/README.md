# USB configuration client

`protocol.py` is the I/O-free wire codec; `client.py` handles one outstanding
request with bounded framing. The command-line entry point opens only the
explicitly selected serial port. It sets DTR/RTS inactive before opening,
performs no explicit reset and never flashes. Some host drivers can still pulse
control lines during open; verify actual board behavior during acceptance.

The borrowed `SerialStream` protocol and command-specific `Client.exchange`
overloads expose concrete replies. `payloads.py` describes the decoded JSON
records, including nullable measurements and empty objects on failed captures.
These annotations preserve the wire/JSON format; decoders validate incoming data.
`identity.py` supplies shared configuration/capability comparison keys for the
configurator and capture tools. Request correlation fields do not establish
board state; active/saved identity and recovery flags remain significant.
Persisted evidence retains its existing list/object formats.
The strict check runs in CI and covers AI measurement processing and its shared validation
dependencies. Live cost observations stay `WorkcostReply` objects until the
recording boundary; replayed JSON is validated before typed window summaries
are returned. Unavailable worker counters remain distinct from zero work.
Check these contracts from the repository root with Python 3.11 or newer:

```console
python -m pip install -r tools/config_transfer/requirements-dev.txt
python -m mypy --config-file tools/config_transfer/mypy.ini
```

The board must run the **configuration application image**, not the acceptance
fixture. Query its identity and capabilities before use. Follow the
[hardware procedure](../../HARDWARE_ACCEPTANCE.md) for physical acceptance.
See [build, protocol and storage contracts](../../firmware/src/config/README.md#transport-and-request-identity).

```console
python -m pip install -r tools/config_transfer/requirements.txt
python -B -m tools.config_transfer probe --port COMx
python -B -m tools.config_transfer query --port COMx
python -B -m tools.config_transfer apply --port COMx --revision 1 --profile configs/nodes/distance-only.json
python -B -m tools.config_transfer save --port COMx --revision 1
python -B -m tools.config_transfer recover --port COMx
```

These are usage examples, not a record of commands executed on the board.
Replace `COMx` with the currently detected board port.
APPLY requires a reviewed node profile and a positive revision.
Preflight with the host planner; target admission is independently authoritative.
OLED and rule/PWM bindings are compiled; complete hardware acceptance remains
open. The original prototype still requests unsupported GPIO2 CS.
RECOVER retries checked cleanup only when the controller reports recovery;
it cannot stop a healthy active plan. Apply a reviewed empty hardware profile
through the normal path when an idle configuration is required. Saved data
remains available for a later boot. SAVE persists the current active revision.

Every command except PROBE, TELEMETRY, WORKCOST and CAPABILITIES discovers the last request ID and queries current
state first. Replies report numeric status: 0 OK, 1 SAVED, 2 UNSAVED, 3 REJECTED,
4 RECOVERY, 5 STALE, 6 CONFLICT, 7 STORAGE, 8 EMPTY. PROBE has `last_request_id`
instead of `status`. Flags and active/saved revisions/CRCs remain separate.
Exit codes: 0 accepted/query result, 2 device-reported unsuccessful result,
1 client/transport error. Invalid command bounds fail before opening a port.

No automatic mutation retry occurs after timeouts or disconnects: the result
may already be active or durable. Reconnect and QUERY before deciding. Never
open a serial monitor or second controller concurrently. IDs increase per boot
and cannot wrap; PROBE still works at UINT32_MAX. The low-level client takes a
borrowed stream with finite timeouts, making its behavior testable without USB.
If matching replies fail decoding and no valid reply follows before the deadline,
the client reports a rejected reply and retains the decoder error as its cause.
Silence or unrelated bytes remain a timeout. Both outcomes require the same
reconnect/query decision; neither triggers a mutation retry.

## One read-only telemetry snapshot

```powershell
python -m tools.config_transfer telemetry --port COMx
```

Use after the separately approved application installation. This reads typed
samples, missing/stale/fault status, queues and health/runtime/configuration
metadata. It does not query storage or consume configuration request IDs, so an
uncertain mutation remains available for an exact retry decision. No automatic
retry occurs. See [wire format and limits](../../firmware/src/services/TELEMETRY.md).
Earlier bench sessions used telemetry; full physical USB/recovery acceptance
still remains open. A successful snapshot does not qualify the whole setup.

## One read-only workload snapshot

`python -B -m tools.config_transfer workcost --port COMx` reads command 9's
bounded per-worker timing, transfer/error counters and empirical stack margins.
It does not query storage, consume mutation IDs, reset counters or change the
profile. Results distinguish a valid snapshot (exit 0) from BUSY/unavailable
(exit 2); unavailable data never becomes a zero-cost measurement. The snapshot
includes profile revision/CRC and binding generation. Compare deltas only within
one verified image, profile and generation; validation preserves identity and
explicit availability. Elapsed worker time includes waits and preemption and is
not CPU utilization. Version 2 additionally carries a coherent native idle/tick
observation. `reply.scheduler_window(previous)` computes sampled non-idle time
over a verified 100 ms..60 s same-profile window; it is not cycle-accurate CPU
usage and cannot isolate interrupts. Version 1 remains supported and reports
the scheduler observation absent. See [wire details](../../firmware/src/services/TELEMETRY.md)
and the host measurement validator in `measurements.py`.

## Read-only Wi-Fi status

`python -B -m tools.config_transfer network --port /dev/cu.usbmodem101`
reads command 10 on a newly built Wi-Fi application. Confirm the current port;
use its COM name on Windows. The command reports the last SDK stage/error,
station and retry flags, last disconnect reason, current IPv4 address and the
absolute board-monotonic retry deadline. A missing IP is `null`. The stage is
the last SDK operation, not proof of connectivity; use `has_ip` and retry state.
Reading status does not connect, reset, apply, save, consume mutation IDs or
expose SSID/passwords. Old and non-Wi-Fi images may time out; no fallback
configuration request or automatic retry is sent.

## Compiled capabilities

`python -B -m tools.config_transfer capabilities --port COMx` reads the board,
node schema, driver IDs, feature bits and bounded capacities without changing
configuration or mutation IDs. Hardware qualification is a separate field and
remains false. See [the capability layout and local UI](../configurator/README.md).
Check these fields against the image actually running at the next hardware
session; a host build record alone cannot establish the board's current state.

## Explicit connection discovery

```powershell
python -B -m tools.config_transfer discover --port COMx --wiring-confirmed
python -B -m tools.config_transfer discover --port COMx --preset bench_uart --wiring-confirmed
```

This requires the D2 discovery feature in the configuration image and an empty hardware configuration
(both devices and interfaces). Confirm the reviewed `bench_i2c` wiring before
using the flag: GPIO0 SDA, GPIO1 SCL, 3.3 V supply/logic, approved equipment.
The command reads capabilities/current state, then requests the compiled preset;
it never stops a running setup. Missing confirmation fails before opening USB.
The resolver also needs the existing planner dependencies in `tools/requirements.txt`.

Output includes raw evidence, matched catalogue identity, protocol and driver
availability. Unknown/ambiguous identities stay explicit; setting values and the
review UI are available in the Configuration Desk. Exit 0 means the attempt completed, even if no known
device responded. There is no automatic retry or apply. Disconnect invalidates
pending evidence. See [limits and wire format](../../firmware/src/discovery/README.md).

The UART option requires the D4 firmware feature (value 4). Confirm the radar
preset: supply 5 V, signals 3.3 V, common ground, TX to ESP RX20 and RX to ESP
TX21, 256000 baud / 8N1. This listens for one second without payload writes.
Clean compatible reports produce a candidate, never verified model identity.
The UI provides explicit model confirmation and separate review/apply. OLED/GPIO
manual selection is a UI draft operation, not a wire probe or actuator test.
