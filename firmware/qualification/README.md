# Build and hardware evidence log

This folder is a designated evidence log. Dated JSON and captures are immutable
historical results, not the current board state or proof for newer source.
Use the [root plan](../../README.md#remaining-work) for outstanding tasks and
the [completed-work log](../../docs/COMPLETED_WORK_LOG.md) for the consolidated
checkpoint narrative. Keep exact hashes, failed runs and source identities.

## Evidence index

| Scope | Record | Boundary |
|---|---|---|
| Consolidated overnight handoff | [handoff](system-overnight-handoff-20261027.json) | Core stability and AI collection; not a full release |
| Core lifecycle/scheduler | [core](esp32c3-scheduler-20261027.json) | USB image, rollback/capacity/reboot; combined radio load separate |
| Radar app-state comparison | [captures](esp32c3-radar-app-comparison-20261027.json) | Initial silence, then healthy connected/disconnected windows; cause unresolved |
| Radar readout on restored firmware | [capture](esp32c3-radar-readout-20261031.json) | No payload in a 60-s interval; requested phone-app attempts not timestamp-confirmed; cause unresolved |
| SEN0390 sensor and application | [isolated light response](sen0390-isolated-light-response-20261031.json), [longer wait](sen0390-startup-wait-20261031.json), [BMP388 first](sen0390-bmp-first-20261031.json), [combined application](sen0390-combined-app-20261031.json) | Clear light response and short combined capture; radar timed out before, during and after; full acceptance remains open |
| SEN0390 Desk rehearsal | [user-operated flow](sen0390-desk-rehearsal-20261031.json), [restoration](sen0390-desk-rehearsal-restore-20261031.json) | Diagnostic firmware, user-reported light response and one live snapshot; full acceptance remains open |
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
  The radar zero-target test is deferred at the user's request.
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
Independent UART observation and module-side supply/contact checks remain open
for diagnosing the earlier intermittent radar silence. Scheduling latency and
counter-report contention also need measurement under combined load.
Repeat lifecycle/resource checks on the changed integrated image and keep scoped
PASS / FAIL / NOT TESTED results. Hardware-qualified remains false until reviewed
sign-off. Do not turn the deferred radar absence test into an automatic next step.
