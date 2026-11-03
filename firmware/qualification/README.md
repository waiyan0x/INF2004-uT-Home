# Build and hardware evidence log

This folder is a designated evidence log. Dated JSON and captures are immutable
historical results, not the current board state or proof for newer source.
Use the [root plan](../../README.md#remaining-work) for outstanding tasks and
the [completed-work log](../../docs/COMPLETED_WORK_LOG.md) for the consolidated
checkpoint narrative. Keep exact hashes, failed runs and source identities.

At the user's request, the temporary Pico diagnostic programs, failed build
and downloaded MicroPython image were removed from the local build folder after
the tests. Their recorded paths and hashes remain historical identifiers, but
the referenced `listener.py`, `sender.py` and MicroPython UF2 are no longer
available locally. Raw capture logs and original Pico flash backups/readbacks
remain in place; those removed files cannot be independently rechecked from the
retained local artifacts.

## Evidence index

| Scope | Record | Boundary |
|---|---|---|
| Radar zero-target observation | [capture](esp32c3-radar-zero-target-20261102.json) | 120/120 fresh snapshots with three absent slots in a user-confirmed clear field; movement and full hardware acceptance remain open. |
| Real radar after reconnection | [capture](esp32c3-radar-reconnected-20261102.json) | 599/600 radar snapshots fresh across two windows; one fault coincided with a user-reported HLK-tool connection, not independently verified. Earlier timeouts and full acceptance remain open. |
| Simulated radar UART input | [capture](esp32c3-pico-simulated-radar-20261102.json), [Pico restoration](pico-simulator-restoration-20261102.json) | ESP decoded Pico-generated reports with expected values; this does not establish the real radar's waveform at the ESP pin. Pico's original flash was restored and read back. |
| Radar reconnection follow-up | [capture](esp32c3-radar-reseat-20261102.json) | Radar timed out throughout two minutes after reconnection; BMP stayed fresh and OLED was readable. Zero ESP UART payload during a later interval does not establish sensor output. |
| Paired Pico / ESP radar observation | [capture](esp32c3-pico-radar-comparison-20261102.json), [Pico restoration](pico-radar-listener-restoration-20261102.json) | Pico observed radar-format reports while ESP timed out, followed by a no-byte interval and recovery; ESP-pin signal and cause remain unverified. Pico's original flash was restored and read back. |
| Three-device cleanup regression | [hardware test](esp32c3-three-device-regression-20261102.json) | BMP, OLED observation, discovery, rollback, profile cycles and restore checks recorded; radar remains intermittently faulted on both images. |
| Cleanup software regression | [Desloppify verification](desloppify-software-20261102.json) | Host, Node and browser regressions; native application build and unchanged aggregate fixed-memory budgets pass. No board actions; wireless/stack/runtime qualification remains open. |
| Consolidated overnight handoff | [handoff](system-overnight-handoff-20261027.json) | Core stability and AI collection; not a full release |
| Core lifecycle/scheduler | [core](esp32c3-scheduler-20261027.json) | USB image, rollback/capacity/reboot; combined radio load separate |
| Radar app-state comparison | [captures](esp32c3-radar-app-comparison-20261027.json) | Initial silence, then healthy connected/disconnected windows; cause unresolved |
| Radar readout on restored firmware | [capture](esp32c3-radar-readout-20261031.json) | No payload in a 60-s interval; requested phone-app attempts not timestamp-confirmed; cause unresolved |
| Radar read-only follow-up | [capture](esp32c3-radar-readonly-20261101.json) | Fresh snapshots and received bytes over two minutes; no fault window; cause unresolved |
| SEN0390 sensor and application | [isolated light response](sen0390-isolated-light-response-20261031.json), [longer wait](sen0390-startup-wait-20261031.json), [BMP388 first](sen0390-bmp-first-20261031.json), [combined application](sen0390-combined-app-20261031.json) | Clear light response and short combined capture; radar timed out before, during and after; full acceptance remains open |
| SEN0390 Desk rehearsal | [user-operated flow](sen0390-desk-rehearsal-20261031.json), [restoration](sen0390-desk-rehearsal-restore-20261031.json) | Diagnostic firmware, user-reported light response and one live snapshot; full acceptance remains open |
| SEN0390 two-hour combined capture | [record](sen0390-two-hour-combined-20261101.json) | Software resets and two-hour snapshots show light and radar faults; original firmware/profile restored; cause and full acceptance remain open |
| SPI polling correction | [comparison](esp32c3-spi-polling-20261027.json) | Tested clocks/deadlines; preserve old negative data |
| Wi-Fi/BLE diagnostics | [radio](esp32c3-radio-diagnostics-20261027.json), [GATT](esp32c3-gatt-settle-20261027.json) | Scan/restart and read/reconnect; SDK allocations remain |
| Remaining SDK semantics | [source audit](esp32c3-wireless-source-audit-20261027.json) | Source findings, not proof every path executed |
| Source/build isolation | [static-host build](esp32c3-static-host-build-20261027.json), [regression](system-software-regression-20261027.json) | Build/test evidence, not hardware acceptance |
| AI studies | [primary](../../ai/qualification/protocol-static-study-20261027.json), [stress](../../ai/qualification/protocol-stress-study-20261027.json) | Frozen benefit gates failed; deployment disabled |
| Prospective varied-goal AI study | [design](../../ai/qualification/protocol-varied-goals-design-20261027.json) | Host preflight and predeclared criteria; no new training data |
| Varied-goal collection | [completion](../../ai/qualification/protocol-varied-goals-completion-20261027.json), [first session](../../ai/qualification/protocol-varied-goals-session-a1-20261027.json), [home session / agent handoff](../../ai/qualification/protocol-varied-goals-session-a2-20261027.json) | 120 verified trials and 15 independent calibrations; order deviation retained, USB-only scope |
| Varied-goal model evaluation | [evaluation](../../ai/qualification/protocol-varied-goals-evaluation-20261027.json) | All six held-out choices match the longest observed passing intervals and the simple rule; no added benefit over that rule, deployment disabled |
| Varied-goal compact model | [C export](../../ai/qualification/protocol-varied-goals-compact-export-20261027.json) | 400 bytes classifier code/read-only tables; 1,132 parity vectors, no mismatches; no linked firmware or board latency evidence, benefit gate unchanged |
| Varied-goal model with active peripherals | [loaded diagnostic](../../ai/qualification/protocol-varied-goals-loaded-probe-20261027.json) | Corrected OFF/ON procedure passed: 2,996 predictions, no mismatches, 85 us maximum four-call batch; failed first procedure retained (498 us maximum). Original firmware/profile restored; full advice, wireless and benefit gates remain open |
| AI continuation and measured-rule comparison | [offline verification](../../ai/qualification/protocol-offline-tools-20261027.json) | Host regression and retained-journal replay; frozen tools used for the completed study |
| Experimental target model | [probe](../../ai/qualification/model-target-probe-20261027.json) | Isolated parity/timing; loaded advice unqualified |

Earlier records and companion `*-build.json` files remain in this folder; raw
serial transcripts are under `logs/`. Counts and object sizes apply to each exact
image. Build success, object size, stack sentinel and physical operation are
different evidence. An unavailable measurement is not zero.

## Peripheral bench observations

Retain these lessons when interpreting current readings:

- A shared-guard holder preempted by a higher-priority worker could lose a
  publication on immediate BUSY. The owned wrapper uses three bounded attempts
  and at most two one-ms delay requests before acquiring ownership. Recursion
  still fails immediately; requested delays are not exact scheduling guarantees.
- The OLED reads the latest cache. Application queue overwrites (health event 4)
  are not UART overruns (event 7), and zero FIFO consumption is expected.
- The claimed 50-byte UART ring was not the tested image: the patched native ring
  has 513 slots/512 usable bytes. Oversized capacity is rejected, not silently
  clamped. Baud is not continuous arrival rate. Check the exact ELF/settings
  before diagnosing overflow from upstream defaults.
- The earlier intermittent radar silence has no established root cause. Fan-off,
  contact changes and phone-app comparisons did not establish causality. A working
  Bluetooth view does not prove the wired UART path. Later recovery does not
  erase earlier failures.
- DC voltage or continuity cannot decode UART traffic or prove supply stability
  at the module. Probe actual labels; connector drawings and wire colours are
  not a pin-order authority. Keep modules off conductive supports.
- Fresh zero-target radar reports are valid data. Moving hardware, uncontrolled
  occupancy or interrupted USB captures cannot establish an empty-scene pass.
  The 2 November 2026 observation passed for the stated setup and two-minute window.
- SPI success does not prove an OLED is attached or readable. The six-pin panel's
  exact controller/internal CS wiring remains unconfirmed; preserve the reviewed
  layout and unused CS reservation until separately qualified.
- BLE client sessions can reuse one physical Windows connection. Verify the
  board's connection/disconnection counters, not just client context exits. The
  passing diagnostic used device access and eight seconds between sessions.
- Oversize internal queue requests need correct partition sizing, not a larger
  total by default; Wi-Fi required a 200-by-8-byte queue. SDK `Static` host options
  do not remove all allocations.

## Reproduce and finish acceptance

Use [target tooling](../../tools/target/README.md) for isolated builds, identity,
backup and intentional updates. Reviewed wiring may stay attached under that
procedure; isolate the board for tests that require it. Download-mode recovery,
large SDK timestamps and kernel time progress do not prove clock accuracy.
Original raw and any normalized logs have separate hashes; never replace them.

Follow [hardware acceptance](../../CONTRIBUTING.md) for remaining external UART,
HC-SR04, PWM/load, controlled power-loss and combined wireless/peripheral tests.
The paired Pico observation establishes a receive mismatch in one capture;
signal checks at the actual ESP pin, controlled input and module-side
supply/contact checks remain open. Scheduling latency and counter-report
contention also need measurement under combined load.
Repeat lifecycle/resource checks on the changed integrated image and keep scoped
PASS / FAIL / NOT TESTED results. Hardware-qualified remains false until reviewed
sign-off; the zero-target observation alone does not complete acceptance.

## Latest-checkout BLE/light service port (2026-11-01)

- [Build](esp32c3-ble-light-services-20261101-build.json): passed BLE-only application build; no hardware access.
- [Memory](esp32c3-ble-light-services-20261101-memory.json): aggregate limits fit; two module reservations need review.
- [Tests and protected sources](esp32c3-ble-light-services-20261101-tests.json): 72 networking tests, 36 qualification tests and unchanged existing runtime/HAL/compatibility source hashes.

## MQTT/HTTPS publisher (2026-11-02)

- [Wi-Fi-only build](esp32c3-mqtt-wifi-20261102-build.json): target qualification passed, build-only fixture, no board access.
- [Memory review](esp32c3-mqtt-wifi-20261102-memory.json): aggregate limits fit; four module reservations need approval.
- [Tests/protected sources](esp32c3-mqtt-wifi-20261102-tests.json): 86 networking tests, 36 build-tool tests, C gate and symbol/hash checks.
- [Combined trial](esp32c3-mqtt-both-20261102-build.json) and [preliminary map](esp32c3-mqtt-both-20261102-memory-analysis.json): failed partition-size check; optimized trial is not final-profile qualification.

## Read-only networking diagnostics and client pack (2026-11-02)

- [Wi-Fi build](esp32c3-network-diagnostics-wifi-20261102-build.json) and [memory](esp32c3-network-diagnostics-wifi-20261102-memory.json): target build passed; aggregate static limits fit, service module approvals open.
- [BLE-only build](esp32c3-network-diagnostics-ble-20261102-build.json) and [memory](esp32c3-network-diagnostics-ble-20261102-memory.json): target build passed; module approvals open.
- [Software/source checks](esp32c3-network-diagnostics-20261102-tests.json): 99 networking and 36 qualification tests, 319-file C gate, protected source/budget and artifact hash verification; no live endpoints.
- [Wi-Fi size attribution](esp32c3-network-diagnostics-20261102-size-wifi.json) and [historical combined reference](esp32c3-network-diagnostics-20261102-size-combined-reference.json): mapped archive ranking; failed builds remain failed, not a controlled A/B comparison.

## Authenticated HTTP Apply software evidence (2026-11-03)

[Tests and protected-source checks](esp32c3-http-config-20261103-tests.json),
[Wi-Fi build](esp32c3-http-config-20261103-wifi-build.json),
[Wi-Fi memory](esp32c3-http-config-20261103-wifi-memory.json),
[BLE build](esp32c3-http-config-20261103-ble-build.json), and
[BLE memory](esp32c3-http-config-20261103-ble-memory.json) are immutable records.
Builds passed; unchanged memory approval checks remain failed for unapproved
service modules despite fitting aggregate ceilings. These are software/build
results only, with no board, flash or live-client acceptance. Existing cfgctrl,
cfgpump, cfgstore and cfgwire source implementations remain byte-identical to
the preceding qualified diagnostics snapshot. See the network guide for the
current contract and remaining physical/resource qualification.

[Networking hardening checks](esp32c3-network-hardening-20261103-tests.json)
record 127 passing offline networking tests, explicit client completion outcomes,
three additional USB/upload/response-deadline regressions, and protected-source
hashes. Both existing target snapshots still verify; no firmware C, reservations
or build inputs changed in this follow-up. The RAM review preserves current
contracts; 351 bytes fixed application headroom and module approvals remain open.
The network guide now contains an unexecuted physical acceptance checklist.

## Current-branch networking merge (2026-11-03)

These records qualify the merge onto `cf94b46`, superseding ZIP-based results for
this source context: [checks](esp32c3-network-merge-20261103-tests.json),
[Wi-Fi build](esp32c3-network-merge-20261103-wifi-build.json),
[Wi-Fi memory](esp32c3-network-merge-20261103-wifi-memory.json),
[BLE build](esp32c3-network-merge-20261103-ble-build.json), and
[BLE memory](esp32c3-network-merge-20261103-ble-memory.json).
Both target builds passed, alongside 127 networking tests, 43 qualification/mode
tests, 14 USB transfer/client regressions and the 351-file C gate. Aggregate RAM
ceilings fit; unchanged module-approval checks remain failed. The merged Wi-Fi
fixed application reservation is 155305/155648 bytes (343 bytes remaining).
These checks accessed no board, serial port, live broker or BLE adapter. Linux CI
and physical acceptance remain separate results. Historical records are retained.
