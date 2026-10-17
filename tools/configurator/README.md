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
   Disconnecting leaves the board running. Recover retries cleanup only when
   recovery is required. To stop a healthy setup, review/apply an empty hardware
   profile. **Apply & save also replaces the saved startup profile**; keep the
   original profile file if you intend to restore it later.

The D2 discovery command requires a newly built application image. Physical USB,
peripheral and persistence acceptance belongs to checkpoint 8. Offline review
uses the host reference catalogue; it is not permission to wire or deploy a
profile. Connected review enforces the narrower compiled target limits.
The original first-prototype profile still uses unsupported GPIO2 CS.

## Device discovery (D3)

The existing Configuration Desk now has a **Device discovery** card. Its layout,
tiles, icons, fonts and themes are retained; `legacy/member5` is untouched.

1. Connect to firmware advertising discovery support. Choose the reviewed I2C
   connection and check the displayed pins, supply and signal voltage. Rewire
   with power disconnected, then reconnect and confirm the checkbox.
2. Select **Detect devices**. Only the approved chip-ID reads at 0x76/0x77 run.
   A busy board stays running: detection neither stops it nor applies an empty
   profile. The card explains the explicit empty-profile workflow if needed.
3. Expand a result to see the observed chip ID, matched descriptor, compiled
   driver support and the sources of the proposed settings. No response, unknown,
   ambiguous and unavailable drivers cannot produce an addition.
4. **Add to profile** appends one device and its readings queue to the draft.
   It reuses an exactly compatible I2C interface; conflicts or duplicate addresses
   are rejected. Existing devices, routes and timing values are preserved. The
   complete candidate passes the shared planner and connected firmware limits.
5. Use the existing **Validate and review → Apply & save** workflow. Adding a
   suggestion itself never sends an apply/save/recovery command. Detect again
   to add another endpoint; each addition consumes its detection token.

Protocol/pins/address come from the catalogue, reviewed preset and observation.
The preset's BMP388 overrides use the previously documented bench starting values
(500 ms interval, 100 ms timeout). Priority/stack come from descriptor defaults;
new queues use the draft's depth. Every displayed setting has a source/reason.
These are deterministic recommendations; no AI/model or new dependency is involved.

Detection tokens last 60 seconds and are bound to the exact draft, connection,
USB session and board state. New detection, edits, connection changes, review,
save/recovery/apply, disconnect, failed snapshots, expiry and observed uptime
regression invalidate them. State/capabilities are rechecked before adding.
Capture details retain the firmware build hash. The wire nonce is **not a boot
ID**, and matching capabilities alone cannot prove an unchanged build after an
unnoticed reset. Always reconnect and detect again after resetting or rewiring.
Adding to a draft is not an operational hardware acceptance result.

Implementation: `discovery.py` builds sourced recommendations and additive drafts;
`workflow.py` retains exclusive USB ownership and single-use evidence;
`static/discovery.js` reuses the dashboard tiles. The existing HTTP server's
connection backlog is 32 so concurrent CSS/font requests do not lose styles on
Windows. Recovery is enabled only when the board reports recovery required,
and the server independently checks that condition.

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
routing; bit 1 means explicit reviewed I2C discovery (D2); bit 2 means the
reviewed receive-only UART observer (D4). Other bits are reserved. The decoder
rejects unknown bits. Manual draft selection does not need a discovery bit.
Schema 2.1 also accepts schema 2.0 sensor/display
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

D3 adds `python -B -m unittest discover -s tests/target -p test_discovery_ui.py -v`
for evidence freshness, additive drafts, mixed interfaces and failure cases.
Its 2026-10-12 validation and simulated-browser results are recorded in the
[D3 checkpoint](../../INTEGRATION_PLAN.md#discovery-checkpoint-d3-local-review-2026-10-12).

## Radar and manual devices (D4)

The approved additions use the same discovery card, tiles, fonts and themes.
No changes were made to the preserved Member 5 toolkit.

1. Choose the reviewed connection and check its wiring description. Rewire with
   power disconnected. Confirm the pins, supply and signal voltage.
2. For **radar**, press **Detect**. The board listens on UART1, RX20/TX21,
   256000 baud, 8N1 for one second. It sends no discovery commands. Three complete
   clean reports produce a *candidate*, including reports with zero targets.
   Confirm the module model to see settings. Noise, silence, incomplete windows
   and ambiguous matches cannot be manually promoted from this result.
3. For **OLED, HC-SR04 or PWM**, press **Prepare manual selection**. This performs
   no peripheral test. Confirm the module and wiring explicitly. OLED additionally
   needs an existing compatible readings queue in the draft; it creates no sensor
   or fake readings. This preset is the tested six-pin SPI arrangement, with GPIO4
   reserved but unwired and no other device on that SPI bus.
4. Review the displayed protocol, wiring, bus settings and descriptor defaults.
   **Add to profile** changes the draft only. Existing entries stay unchanged;
   duplicate buses/devices, pin conflicts, task limits and incompatible routes
   are errors. Use **Validate and review** and **Apply & save** separately.

PWM adds a **disabled** generic output on GPIO1. It needs schema 2.1.0; the tool
will not silently change the draft's schema. No load identity, duty or trigger
rule is guessed. Add and review a suitable control rule separately before
explicitly enabling it. HC-SR04 needs its echo voltage divider. These presets
are alternative reviewed layouts: OLED's GPIO4 reservation conflicts with the
HC-SR04 preset, and PWM1 conflicts with the I2C preset's SCL1.

Manual selection is user testimony, not measured identity or operating proof.
After applying, OLED still requires a visual check and GPIO equipment requires
its separate acceptance test. Radar stream compatibility does not prove an exact
model. Driver availability and hardware qualification remain separate.

Both measured results and manual confirmations retain the D3 session/draft/state
checks. Confirming rotates the token but never extends its original 60-second
expiry; adding consumes it. Reconnect, observed restart, board/draft changes,
rewiring or expiry require a fresh selection/check. A boot with no observed
uptime regression can still escape detection; the earlier reset limitation
remains. Manual selection uses USB state/capability queries, without opening
peripheral buses. I2C/radar observation still requires empty hardware configuration.

New firmware is required for radar observation (feature value 4). An older build
can still prepare manual choices if it advertises the necessary driver/schema.
The CLI also accepts `discover --preset bench_uart --wiring-confirmed`; it
prints evidence/candidates and does not confirm or apply a device.

Checks: `tests/discovery/test_uart_observations.py`,
`tests/target/test_discovery_confirm.py`, `tests/target/uartdisc.c`, the production
C/Python wire-parity test, and the Node presentation tests. A fake-board Edge
walkthrough covers radar confirmation, an OLED queue choice, disabled PWM,
separate review/apply and disconnect in both themes at desktop/mobile widths.
See the [D4 evidence](../../firmware/qualification/esp32c3-discovery-d4-20261012.json).
Physical discovery acceptance is still pending; no AI training is involved.

## Preserved incoming toolkit

The later `system-integration` changes merged into upstream PR #1 are retained
under [legacy/member5](../../legacy/member5/README.md), including recommendation
imports, health snapshots and bundle verification. That schema-1.0/S3 workflow
is separate from this schema-2.0/2.1 C3 USB application.
