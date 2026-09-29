# USB configuration client

`protocol.py` is the I/O-free wire codec; `client.py` handles one outstanding
request with bounded framing. The command-line entry point opens only the
explicitly selected serial port. It sets DTR/RTS inactive before opening,
performs no explicit reset and never flashes. Some host drivers can still pulse
control lines during open; verify actual board behavior during acceptance.

The board must eventually run the **configuration application image**, not the
acceptance fixture. See [build, protocol and storage contracts](../../firmware/src/config/USB_STORAGE.md).
The latest board image has not been replaced in this checkpoint.

```console
python -m pip install -r tools/config_transfer/requirements.txt
python -B -m tools.config_transfer probe --port COM8
python -B -m tools.config_transfer query --port COM8
python -B -m tools.config_transfer apply --port COM8 --revision 1 --profile configs/nodes/distance-only.json
python -B -m tools.config_transfer save --port COM8 --revision 1
python -B -m tools.config_transfer recover --port COM8
```

These are examples for the final hardware session, not commands executed by this
checkpoint. APPLY requires a reviewed node profile and a positive revision.
Preflight with the host planner; target admission is independently authoritative.
OLED and rule/PWM bindings are now compiled; physical hardware remains
unqualified. The original prototype still requests unsupported GPIO2 CS.
RECOVER performs checked cleanup to an empty live plan, including a healthy
active plan; saved data remains available for a later boot. SAVE persists the current active revision.

Every command except PROBE, TELEMETRY and CAPABILITIES discovers the last request ID and queries current
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

## One read-only telemetry snapshot

```powershell
python -m tools.config_transfer telemetry --port COM8
```

Use after the separately approved application installation. This reads typed
samples, missing/stale/fault status, queues and health/runtime/configuration
metadata. It does not query storage or consume configuration request IDs, so an
uncertain mutation remains available for an exact retry decision. No automatic
retry occurs. See [wire format and limits](../../firmware/src/services/TELEMETRY.md).
Physical USB behavior still needs acceptance; this command has not been run on
the connected board during checkpoint 6a.

## Compiled capabilities

`python -B -m tools.config_transfer capabilities --port COM8` reads the board,
node schema, driver IDs, feature bits and bounded capacities without changing
configuration or mutation IDs. Hardware qualification is a separate field and
remains false. See [the capability layout and local UI](../configurator/README.md).
This command is provided for the later hardware session; it was not run on the
board during these software checkpoints.
