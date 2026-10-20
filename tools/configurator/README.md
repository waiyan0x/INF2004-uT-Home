# Local configurator

From the repository root:

```powershell
python -m pip install -r tools/requirements.txt -r tools/config_transfer/requirements.txt
python -B -m tools.configurator --port 8080
```

Open `http://127.0.0.1:8080`. Stop the server with Ctrl+C. The UI uses Python's
standard HTTP server and local HTML/CSS/JavaScript; no Node build or new web
framework is required. Offline review needs only the planner dependencies.

1. Load an example or JSON file and edit instances, wiring, timing and routes.
2. Select **Validate and review** to see resource demand, devices and rules.
3. For deployment, explicitly connect to the application firmware's serial port,
   then review again against its reported compiled capabilities.
4. **Apply & save** replaces the whole plan and attempts persistent storage.
   Check both the result and active/saved revisions. Retry saving is a separate,
   explicit operation; it does not reapply the profile.
5. Live readings show source identity, freshness, health and queue/runtime state.
   Disconnecting leaves the board running. Stop/recover clears the live plan;
   its saved profile is retained and can restore on the next boot.

The currently installed board image predates this application. Physical USB,
peripheral and persistence acceptance belongs to checkpoint 8. Offline review
uses the host reference catalogue; it is not permission to wire or deploy a
profile. Connected review enforces the narrower compiled target limits.
The original first-prototype profile still uses unsupported GPIO2 CS.

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
routing; other bits are reserved. Schema 2.1 also accepts schema 2.0 sensor/display
profiles. UART20/21 follow the board's fixed UART contract, separately from the
ordinary GPIO mask. Compiled support is shown separately from hardware
qualification, which remains **false**. `configs/builds/target-pending.json`
remains empty.

## Source and verification

Adapted Member 5's cached stdlib server/workflow design from `system-integration`
commit `bab09246a477dbedef36a55dad7cd319bac1c1ab` (CharmTzy). The current UI
uses the shared schema/planner and real USB client instead of the older branch's
validation and filesystem bundle workflow. This is not a merge or a claim that
the cached commit is the latest remote branch.

The Configuration Desk appearance (cards, icons, Roboto fonts, responsive layout
and light/dark themes) is recovered from `6a16f2473a348fda0d95eedf12c3839b0010f687`.
Its presentation uses the current C3 schema and USB workflow, not the legacy
server or sample telemetry. Device tiles describe the JSON draft; live pills
appear only from connected firmware snapshots and retain freshness labels.
Font provenance is in [assets/fonts/README.md](static/assets/fonts/README.md).

Run presentation regression checks with `node --test tests/target/test_dashboard.mjs`
and HTTP/workflow checks with
`python -m unittest discover -s tests/target -p test_configurator.py`.

`tests/target/test_configurator.py` covers offline review, single-use candidates,
changed board/capabilities, uncertain replies, HTTP boundaries and rule support.
Existing C/Python fixtures verify capability framing and observation isolation.
Headless Edge checks exercise review, rules, edits, errors and responsive layout.
See [fresh evidence](../../firmware/qualification/esp32c3-ui-rules-20260928.json).

## Preserved incoming toolkit

The later `system-integration` changes merged into upstream PR #1 are retained
under [legacy/member5](../../legacy/member5/README.md), including recommendation
imports, health snapshots and bundle verification. That schema-1.0/S3 workflow
is separate from this schema-2.0/2.1 C3 USB application.
