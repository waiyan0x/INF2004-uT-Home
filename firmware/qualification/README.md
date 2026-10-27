# Build and hardware evidence

These dated JSON records and captures are immutable evidence, not a claim that
every newer source revision has been tested on the board. Build-only results
remain separate from physical observations. Keep new runs in new files.

## Evidence index

The [overnight handoff](system-overnight-handoff-20261027.json) consolidates
272 usable AI runs / 99,552 observations and 45 minutes / 3,996 paired stability
observations on the retained USB core. Both frozen models fail their benefit gate;
neither is deployed. The original three-device profile is restored and serial is
closed. This is scoped acceptance, not full wireless/static/AI product qualification.

The [final read-only GATT diagnostic](esp32c3-gatt-settle-20261027.json) **passed**.
Its board log reports 2 connections,
2 disconnections and 4 reads.
Earlier [sandbox](esp32c3-gatt-20261027.json),
[active-address](esp32c3-gatt-retry-20261027.json),
[static-host](esp32c3-gatt-static-20261027.json) and
[short-disconnect](esp32c3-gatt-access-20261027.json) results are retained.
Windows device access was necessary for the client to open the known board;
closing a client session did not immediately close the physical radio link.
The final client allows eight seconds between sessions, retaining the unchanged
two-connection/four-read board gate. SDK allocations remain disqualifying.

The [latest static-host build experiment](esp32c3-static-host-build-20261027.json)
binds 183 target tests, the 283-file C gate, actual SDK-setting checks and fresh
ordinary/diagnostic builds. Static host globals add 4,352 SDK data bytes and
4,294 total RAM bytes; the diagnostic passes the unchanged budget at 251,813 bytes.
The final GATT bench record above captures allocation counts and connection behavior separately.
The preceding [GATT software check](esp32c3-gatt-final-software-20261027.json) binds
182 target tests, the 283-file C gate and fresh isolated/ordinary builds, with
unchanged aggregate RAM limits. The ordinary image excludes radio/model entry
points. Actual GATT execution remains a separate record. The preceding
[software regression](system-software-regression-20261027.json) binds
the 180-test target suite, 119-test AI suite, full 281-file C gate and fresh core
builds. The [wireless source audit](esp32c3-wireless-source-audit-20261027.json)
identifies remaining static task/timer and SDK compatibility work. These records
do not by themselves establish board execution or complete wireless operation.

The [scheduler/core checkpoint](esp32c3-scheduler-20261027.json) records two
1,699-check fixture boots, twenty cycles, rollback, six rejection cases, reboot,
host disconnect and a 20-minute stability capture. All 1,776 paired observations
kept both sensors fresh and forbidden allocation/object counters zero. Its fixed
internal RAM is 230,152 bytes. Physical OLED and whole-system acceptance remain
separate. A failed first capture summary was retained and safely rerun; unavailable
live worker-stack margins are null, with actual quiescent margins recorded separately.

Latest collection-image evidence is the
[corrected SPI/core checkpoint](esp32c3-spi-polling-20261027.json): two 1,694-check
fixture boots, twenty profile/discovery cycles, rollback, parser/queue rejection,
reboot and a 30-second host-client disconnect. Its linked internal RAM use is
230,090 bytes; application heap and late-object counters stayed zero. The
six-clock before/after comparison isolates the corrected FIFO polling delay.
The [fixed network core](esp32c3-network-core-20261027.json) records the preceding
startup-owner correction. Neither result enables radios. Physical OLED, USB
unplug, radio coexistence and wider hardware acceptance remain open.
Both subsequent AI studies are complete; consult the root README for the active
bench/capture owner before board access. Workload revisions are not firmware changes.

The [radio comparison](esp32c3-radio-diagnostics-20261027.json) records successful
Wi-Fi scan/stop/restart and PC-observed BLE advertising. It also records SDK heap
allocations; separate radio feasibility does not qualify the integrated static system.

The preceding
[allocation audit](esp32c3-allocation-audit-20261027.json): zero observed SDK heap
events/kernel allocator calls during application startup and normal operation;
zero post-startup kernel-object creations through twenty cycles, rollback and
reset/restore. The idle stack is now a fixed 1 KiB buffer. Two ordinary fixture
boots passed 909 checks each; the final revision is 342. The 230,190-byte fixed
internal-RAM footprint passes the declared budget. See the record for hook scope,
source/build evidence, backing artifacts and deferred stack/wireless/AI work.

The preceding
[static-core checkpoint](esp32c3-static-core-20261027.json): fixed parser pools,
permanent worker stacks, checked storage layout, two 909-check runtime-fixture
boots, twenty discovery/profile/save cycles, rollback and reboot restore at
revision 260. The fixed-map budget passes; whole-system allocation/object/stack,
wireless and AI deployment qualification remain open. Native UART injection was
explicitly excluded because its isolated fixture requires peripherals removed.
The earlier failed combined fixture capture is retained in the checkpoint.
The [isolated UART fixture](esp32c3-static-core-20261027-isolated-uart-build.json)
also builds with its fault hook intact; it was not installed or board-tested.

The preceding
[integration bench regression](esp32c3-integration-bench-20261026.json): twenty
discovery/empty/original cycles, active BUSY and target capacity rejection,
followed by failed BMP activation at the previously observed absent 0x76 address
and successful rollback. Original settings are restored at revision 179.
The OLED was user-confirmed readable/updating. This does not qualify heap-free
execution, permanent worker objects, injected cleanup failure or power-loss
recovery. The [AI preparation record](../../ai/qualification/static-adaptation-20261026.json)
keeps model export and remaining release gates separate.

| Area | Recorded scope | Record |
|---|---|---|
| Pinned kernel | Build, then C3 revision 0.4 / 4 MB identification and startup/reset captures | [build](esp32c3-kernel-20260928.json), [board](esp32c3-board-20260928.json) |
| Worker port | 369 checks on each of two resets | [runtime](esp32c3-runtime-20260928.json) |
| BARR refactor | Two 369-check resets and repeated four-slot allocation/release | [regression](esp32c3-barr-20260928.json), [signed audit](../../tools/c_standard/AUDIT.md) |
| Typed activation | 514 checks on each of two resets; replacement, failure and recovery | [activation](esp32c3-activation-20260928.json) |
| I2C / BMP388 | Software, compiler and build checks | [I2C](esp32c3-i2c-20260928.json) |
| SPI / OLED | Software, compiler and build checks | [SPI](esp32c3-spi-20260928.json) |
| UART / radar | Software, compiler and build checks | [UART](esp32c3-uart-20260928.json) |
| GPIO / distance / PWM | Software, compiler and build checks | [GPIO](esp32c3-gpio-20260928.json) |
| Configuration framing/storage | Portable core and injected failure checks | [core](esp32c3-config-core-20260928.json) |
| Target admission/binding | Parsing, resource admission and simulated driver lifecycle | [admission](esp32c3-admission-20260928.json) |
| USB and flash adapters | Application/fixture builds and host transport/storage checks | [USB/storage](esp32c3-usbstore-20260928.json) |
| Telemetry / health | Wire parity, sample freshness and monitoring | [telemetry](esp32c3-telemetry-20260928.json) |
| Display renderer | Software rendering and application/fixture builds | [display](esp32c3-display-20260928.json) |
| UI / rules | Local workflow, capabilities and bounded output rules | [UI/rules](esp32c3-ui-rules-20260928.json) |
| I2C discovery | Bounded observer, host resolution and target builds | [discovery](esp32c3-discovery-20261012.json) |
| UART/manual discovery | Radar observer, confirmations and target builds | [D4](esp32c3-discovery-d4-20261012.json) |
| Cleanup ownership / earlier bench | Two 881-check fixture boots, verified application update, discovery, profile changes, rejection/rollback and reset restoration; combined stability failed | [26 October results](esp32c3-cleanup-20261026.json), [application build](esp32c3-cleanup-20261026-app-build.json), [fixture build](esp32c3-cleanup-20261026-fixture-build.json) |
| Shared-guard handoff / latest combined bench | Two 895-check fixture boots; 600 seconds with all 4,380 BMP/radar snapshots fresh, 1,200 BMP publications and no new device/UART errors or missed heartbeats | [results](esp32c3-shared-handoff-20261026.json), [application build](esp32c3-shared-handoff-20261026-app-build.json), [fixture build](esp32c3-shared-handoff-20261026-fixture-build.json) |
| System-integration pull with local work retained | 218 host tests, 221-file C gate and fresh application/fixture builds; UART native objects use fixed-storage callbacks and each ELF reserves 1280 bytes for their storage; neither image flashed | [integration](esp32c3-system-integration-20261026.json), [application build](esp32c3-system-integration-20261026-app-build.json), [fixture build](esp32c3-system-integration-20261026-fixture-build.json) |
| Integration image, later physical regression | Full backup, verified flash/configuration preservation, twenty discovery/profile cycles, invalid-capacity rejection, failed-activation rollback and fresh restored samples; static-memory qualification remains open | [bench](esp32c3-integration-bench-20261026.json) |

Companion `*-build.json`, `*-application-build.json`, `*-app-build.json` and
`*-fixture-build.json` files retain exact tool versions, source/artifact hashes
and compiler profiles. Captures are under `logs/`. Their static type/object sizes
are historical accounting, not available RAM or measured stack headroom.
Older manual-review paths resolve through the [documentation migration map](../../docs/README.md#historical-references).

## Peripheral bench observations

**Earlier shared-guard result, 26 October:** after the user deferred further zero-target checks,
investigation found that immediate shared-guard rejection could discard a BMP
publication when a higher-priority task preempted the holder. The kernel wrapper
now permits three acquisition attempts with at most two one-ms delay requests
before ownership. Recursion still returns BUSY immediately; actual delay depends
on scheduling. The [dated record](esp32c3-shared-handoff-20261026.json) separates
the source finding, software/board regression and measured behavior.

| Combined profile observation | Before: 120 s | After: 600 s |
|---|---:|---:|
| Unpublished BMP attempts between endpoint samples | 50 of 238 | 0 of 1,200 |
| Stale BMP snapshots | 21 of 877 | 0 of 4,380 |
| Maximum observed BMP sample age | 1,975 ms | 500 ms |
| New missed-heartbeat events | 8 | 0 |
| New timeout / UART / device-I/O errors | 0 | 0 |

All 4,380 post-update radar snapshots were fresh; maximum observed age was 59 ms.
The first 120 seconds of the new capture were also clean. Application FIFO
overwrites continued as expected for queues with no consumer; these are distinct
from UART receive overruns. The OLED reads the latest cache, and the user confirmed
it remained readable and updating. Active/saved revision 87 / CRC 3623560383 was
unchanged, and the configuration partition readback matched the pre-update backup.

The full 194-file C gate, 76 target host tests, 18 runtime tests and both real
target builds passed. Two fixture boots each passed 895 checks, including forced
preemption of a shared-guard holder. This establishes one combined-profile result,
not universal timing reliability or the cause of the earlier UART failures.
Radar absence acceptance is **deferred at the user's request**. The uncontrolled
target counts in this capture are not an absence test. Hardware-qualified remains
false; old AI study/source identities are preserved and new training requires the
updated build identity and fresh runs.

The later [post-fix AI workload study](../../ai/qualification/protocol-study-shared-20261026.json)
completed 96 fresh runs on this same image. Its post-warmup BMP388/radar
observations stayed fresh, with no new missed-heartbeat, UART or device-I/O
events. Its three 1 MHz SPI groups accumulated 1,923 global device-timeout events;
per-device attribution and transfer durations were not measured. The original
profile was restored active/saved at revision 138 / CRC 3623560383, with fresh
BMP388/radar data in the subsequent query. This expands the observed workloads
without qualifying all configured clocks, absence detection or AI deployment.

### Earlier peripheral observations

These observations were recorded in the hardware guide for 29 September 2026;
they are partial functional checks, not a completed acceptance report.

| Setup | Observed result | Limit |
|---|---|---|
| BMP388 | 0x77, I2C 100 kHz, 500 ms interval, 100 ms timeout started successfully | Earlier 20 ms initialization timeout failed; bus sharing/accuracy not qualified |
| BMP388 + six-pin OLED | SPI 4 MHz; 120.12 s, 240 new BMP samples, no device timeouts/I/O errors/failed workers in that interval; user confirmed live text | 1 MHz refresh timed out; exact OLED controller and internal CS wiring unconfirmed |
| Saved BMP/OLED restart | Revision 5 and CRC restored after USB power cycle; screen resumed, 21 new BMP samples with no recorded errors in follow-up | One functional restart, not power-loss storage qualification |
| BMP/OLED + LD2450 V1.1 | UART RX20/TX21, measured 5 V module supply; revision 7 restored; user confirmed moving positions | USB-fed power observation is not a general supply/current qualification |
| Combined radar interval | 120.3 s, 57 snapshots, 1350 new radar reports and 241 BMP samples; fresh, no new device/UART/worker failures | Two startup timeouts were already present; their source/timing is unresolved |

Queue-overwrite counts matched the configured drop-oldest queues. These are real
historical events, not evidence of a broken sensor or an all-clear health report.

**Unresolved empty-scene test:** The OLED showed `READING UNAVAILABLE`; the user
also moved/touched boards or cables. Counters reached 113 device timeouts and
13 UART frame errors. A disturbed connection is possible, not established.
Repeat with stationary hardware: valid zero-target reports should stay FRESH
with three ABSENT slots. Only one target was observed; accuracy/multi-target
acceptance remains open. Interrupted USB runs do not count as clean runs.

## Reproduce and finish acceptance

On 26 October 2026, a full flash backup and the corrected application update
succeeded with the reviewed BMP388/six-pin OLED/LD2450 wiring left intact. The
cleanup fix released the previously retained UART instance. Initial physical
discovery observed BMP388 chip ID 0x50 at 0x77 and 11 valid radar reports in one
second; the board's ELF digest matched the retained build. The user confirmed
readable, updating OLED output.

The subsequent 600-second combined capture **failed stability acceptance**:
47 stale BMP388 observations and 477 radar fault observations among 2,401
snapshots. It added 27 missed heartbeats, 478 timeouts and 11 UART framing events;
7,516 queue overwrite events are separate. Radar communication loss persisted
through UART reopen, ESP reset and a user-confirmed full power cycle. The
empty-view attempt therefore did not establish sustained valid zero-target
behavior. Supply/connection diagnosis remains open. Raw local evidence and hashes
are in the dated record; do not replace it with later recovery results.

The [radar isolation follow-up](esp32c3-radar-isolation-20261026.json) records
user-reported DC readings of 5.075 V at the ESP source and 4.817 V at the radar's
supply rails. These were not simultaneous or measured at the module connector.
A 120-second radar-worker-only run still produced 107 fault and 14 fresh
observations. All modules remained physically connected, so this isolates
software workload, not electrical loading. The combined profile was subsequently
restored and saved as revision 26. Module-side power/contact checks remain open.

The later [fan-off retest](esp32c3-fan-off-20261026.json) followed the user's
reported zero-ohm signal check. All 241 BMP388 snapshots were fresh, while all
241 radar snapshots were fault/timeout; 480 new timeout events were recorded
over 120 seconds. Switching the fan off did not restore communication. Empty
revision 27 was subsequently saved successfully for a power-off GPIO20 socket
contact check. This does not establish whether the underlying cause is the
connection, supply, module or UART adapter.

The [TX contact retest](esp32c3-radar-contact-20261026.json), after moving the
GPIO20 termination from A:b35 to A:a35, likewise had 241 fresh BMP388 and 241
radar fault snapshots in 120 seconds. Combined revision 28 is saved.

The subsequent user-observed Bluetooth check in HLKRadarTool tracked movement
in real time. The supplied screenshot identifies `HLK-LD2450_E00E`, firmware
`2.04.23101915`, and one target at 0.27 m, -5.08 degrees and 0.24 m/s. Its baud
field reads `Select Baud Rate`; that placeholder does not identify the current
UART baud. This establishes observed Bluetooth tracking, not the wired output
or ESP receive path. The user then confirmed selecting 256000 baud in the app,
matching the retained ESP profile, and rebooting the radar. No firmware update
or factory reset was requested.
Hi-Link's [app tutorial](https://h.hlktech.com/download/HLK-LD2450-24G/1/HLK-LD2450%E4%BD%BF%E7%94%A8%E6%95%99%E7%A8%8BV1.1.pdf)
documents selecting the baud and rebooting the module afterwards.

The [post-baud retest](esp32c3-radar-baud-20261026.json) still failed: one fresh
radar snapshot and 240 fault snapshots over 120 seconds, with 477 new timeout
events. All 241 BMP388 snapshots remained fresh. A separate one-second UART
discovery after closing the workers received zero bytes, with no receive errors.
The combined profile was restored and saved as revision 30. The intermittent
valid report confirms some successful reception; it does not identify the
cause of the subsequent silence. Next is a user DC voltage measurement at the
GPIO20 receive strip (Board A b35) relative to common ground. The user subsequently
reported `3.015`; interpreted as volts from the requested DC-volts test, this
shows the observed line was not held at ground. A multimeter reading alone
cannot qualify UART waveforms or decode reports. A spare ESP is available but
not yet identified; independent UART observation remains pending.

The user later reported OLED radar readings returning after disconnecting the
phone app. The [recovery observation](esp32c3-radar-return-20261026.json) captured
120 seconds with all 241 radar snapshots fresh, one target in each, and no new
timeouts, framing errors or UART overruns. The radar sequence advanced by 1,348.
There was one stale BMP388 snapshot and one missed heartbeat; application queue
overwrites increased by 1,684. The combined profile remained revision 30 with
the same CRC. This establishes live reception during the interval, not a repair
or Bluetooth causality. The assistant made no firmware/profile/wiring changes
for this run.

The subsequent [connected-app comparison](esp32c3-radar-bluetooth-20261026.json)
also had 241 fresh radar snapshots over 120 seconds, with no new timeout,
framing-error or UART-overflow events. The user confirmed reconnection and was
asked to keep live tracking open; there is no independent Bluetooth state log.
The ESP did not reset between these captures, and revision 30 / CRC remained
unchanged. Phone reconnection therefore did not reproduce the radar fault in
this test. The connected run had 12 stale BMP388 snapshots and 12 missed
heartbeats; application queue overwrites increased by 1,652. These observations
do not establish the source of either intermittent fault. Next is a stationary
empty-view radar check while reception is working, with the phone app
disconnected and no wiring or firmware changes.

After the protocol study restored combined revision 87, a query initially found
radar faults. The user then reported normal OLED readings. The subsequent
[60-second status check](esp32c3-radar-status-20261026.json) captured 121 fresh
radar and 121 fresh BMP388 snapshots, with no new timeouts, UART errors or missed
heartbeats. Radar sequence advanced by 674; sample ages were 0..88 ms. There
were 82 fresh zero-target and 39 fresh one-target snapshots. Application queue
overwrites increased by 848, separately from UART overruns. Profile revision 87,
CRC 3623560383 and runtime generation 58 remained unchanged. No configuration or
firmware changes were made for this check. Reception had recovered, but the
cause of the intermittent failure remains unresolved. Scene occupancy was not
controlled, so the zero-target observations do not complete absence acceptance.

The following [empty-view attempt](esp32c3-radar-empty-20261026.json) began after
the user confirmed they would clear the view for 90 seconds. The capture allowed
15 seconds to leave, then evaluated the following 60 seconds. All 151 radar and
BMP388 snapshots over the complete 75-second capture stayed fresh; no new
timeouts, UART errors or missed heartbeats occurred. The predefined evaluation
window contained 18 one-target and 103 zero-target snapshots. The target was
reported at roughly 14..24 seconds, with y=2431..3673 mm, followed by 51 seconds
of continuously fresh zero-target reports. Thus the full planned 60-second
absence criterion failed. The user subsequently
[confirmed the view was empty by 15 seconds](esp32c3-radar-empty-clarification-20261026.json).
The 18 one-target snapshots after settling were therefore unexpected against
the reported empty scene. Their source is unproven; the later zero-target
reports do not erase them. Revision 87 and wiring stayed unchanged by the
assistant. The original intermittent UART failure remains unresolved.

The user then described an object-dense scene, suspected their chair, and pointed
the module upward. After confirming readiness for a stationary, ceiling-facing
check, the [separate diagnostic](esp32c3-radar-ceiling-20261026.json) recorded
another 75 seconds, again excluding the first 15 seconds from evaluation. All
151 radar and BMP388 snapshots were fresh. Within the 60-second evaluation
window, 109 of 121 radar snapshots reported one target and 12 reported zero;
radar age was 0..84 ms and sequence advanced by 674. The reported target distance
was approximately 0.36..0.70 m, which does not identify a physical object.
No new device timeouts or UART errors occurred, but one missed heartbeat and
1,060 application queue overwrites occurred across the full capture. Revision
87 / CRC 3623560383 and runtime generation 58 remained unchanged. This failed
the ceiling-facing zero-target criterion; it is not a normal-mounting acceptance
result, a diagnosis of the chair/ceiling, or an AI training run.

The user attributed the earlier nearby target to USB and requested a
[handheld repeat](esp32c3-radar-handheld-20261026.json). The attribution was not
verified, and hand/board motion was not instrumented. Every one of the 151 radar
snapshots over 75 seconds was fresh and reported two targets, including all
121 snapshots after the predefined 15-second settling period. There were no new
device timeouts or UART errors. However, six BMP388 snapshots were stale and
five new missed-heartbeat events occurred; application queue overwrites increased
by 1,039. Revision 87 / CRC 3623560383 and runtime generation 58 were unchanged.
This is a handheld diagnostic, not stationary absence acceptance or AI training
data. It did not establish a no-target condition or identify the detections'
source; combined timing/freshness faults remain open.

**Review of the 50-byte-ring hypothesis:** The untouched dependency header has
the upstream value 50, but the retained application's staged header has 513
after `0001-uthome-c3-resources.patch`. Its hash-verified ELF independently shows
`T_SER_BUFF.data` with upper bound 512 (513 slots; 512 usable). That ELF digest
matches the board's last discovery response. The adapter rejects an oversized
request with `UARTIO_INVALID`; it does not silently clamp the capacity. The
radar worker is configured at 10 ms, not 100 ms. Nominal input is 300 bytes/s
in 30-byte bursts, not a continuous 256000-baud stream. Event 4 counts application
queue overwrites; event 7 counts reported UART receive overruns and remained zero
in the cited runs. Scheduling latency and counter-report contention remain
measurement limitations. The specific small-ring explanation is contradicted;
this does not exclude another UART HAL, interrupt or scheduling defect. Local
audit details are retained in
`build/hardware-acceptance-20261026/uart-buffer-hypothesis-review.json`.

Ten timing-profile changes, target rejection of reserved GPIO2, rollback from
absent BMP388 address 0x76 and three software-reset restorations succeeded for
their configuration-lifecycle scope. They do not negate the sensor faults or
qualify interrupted-save behavior. The original combined profile was restored
and saved as revision 24, CRC 3623560383 at the end of that sequence; enumerate
and query again before assuming the board's current state.

Use [target tooling](../../tools/target/README.md) for fresh isolated builds,
source verification, backup and intentional flashing. Earlier runs are under
`%USERPROFILE%/esp`; their records identify exact directories. Normalized repo
logs and original raw captures have separate hashes. Do not replace either.

The initial kernel reset stayed in download mode; a watchdog reset recovered
boot. Later complete startup captures show alternating A/B progress. ESP-IDF
startup timestamps were unusually large and are not timing evidence. Kernel
time advanced 101 ms for a requested 100 ms delay: progress, not clock accuracy.

Still required: radar absence (user-deferred), HC-SR04 pulse/voltage checks, PWM/load
tests, broader combined-load timing, physical disconnect/recovery, flash interruptions,
repeat profile/reboot checks and heap/stack high-water measurements. Follow the
[hardware acceptance guide](../../CONTRIBUTING.md) and retain honest PASS / FAIL /
NOT TESTED results. Hardware-qualified remains false until a reviewed sign-off.
