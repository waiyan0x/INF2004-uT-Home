# Local configurator

From the repository root:

```powershell
python -m pip install -r tools/requirements.txt -r tools/config_transfer/requirements.txt
python -B -m tools.configurator --port 8080
```

Open `http://127.0.0.1:8080`. Stop the server with Ctrl+C. The UI uses Python's
standard HTTP server and local HTML/CSS/JavaScript; no Node build or new web
framework is required. Offline review needs only the planner dependencies.
The interface uses one fixed colour palette.

1. In **Profile**, choose an example or import JSON. Open **Edit profile JSON**
   for custom instances, wiring, timing and routes. Optional discovery is under
   **Add a device**; interfaces, queues and the catalogue are under
   **Wiring & resources**.
2. Select **Validate and review** to open **Review & apply** with the requested
   resources, devices and rules. Errors remain visible above all three views.
3. For deployment, explicitly connect to the application firmware's serial port,
   then review again against its reported compiled capabilities.
4. **Apply & save** replaces the whole plan and attempts persistent storage.
   Check both the result and active/saved revisions. Retry saving is a separate,
   explicit operation; it does not reapply the profile.
5. **Live monitor** shows source identity, freshness and health. Open its
   observation and runtime details when needed.
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
python -B -m unittest discover -s tests/target -p test_discovery_ui.py -v
python -B -m unittest discover -s tests/target -p test_discovery_confirm.py -v
python -B -m unittest discover -s tests/discovery -v
node --test tests/target/test_dashboard.mjs
```

Checks cover review-token expiry, additive drafts, state changes, conflicts,
uncertain deployment, HTTP boundaries, C/Python parity and presentation. Fake-board
Edge walkthroughs cover themes and desktop/mobile flows. These are not physical
device detection. Evidence: [UI/rules](../../firmware/qualification/esp32c3-ui-rules-20260928.json)
and [D4](../../firmware/qualification/esp32c3-discovery-d4-20261012.json).

The incoming Member 5 toolkit remains under [legacy/member5](../../legacy/member5/README.md).
Its schema-1.0/S3 bundle workflow is separate from the current C3 USB application.
