# Build and hardware evidence

These dated JSON records and captures are immutable evidence, not a claim that
every newer source revision has been tested on the board. Build-only results
remain separate from physical observations. Keep new runs in new files.

## Evidence index

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

Companion `*-build.json`, `*-application-build.json`, `*-app-build.json` and
`*-fixture-build.json` files retain exact tool versions, source/artifact hashes
and compiler profiles. Captures are under `logs/`. Their static type/object sizes
are historical accounting, not available RAM or measured stack headroom.
Older manual-review paths resolve through the [documentation migration map](../../docs/README.md#historical-references).

## Peripheral bench observations

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

Use [target tooling](../../tools/target/README.md) for fresh isolated builds,
source verification, backup and intentional flashing. Earlier runs are under
`%USERPROFILE%/esp`; their records identify exact directories. Normalized repo
logs and original raw captures have separate hashes. Do not replace either.

The initial kernel reset stayed in download mode; a watchdog reset recovered
boot. Later complete startup captures show alternating A/B progress. ESP-IDF
startup timestamps were unusually large and are not timing evidence. Kernel
time advanced 101 ms for a requested 100 ms delay: progress, not clock accuracy.

Still required: stationary radar absence, HC-SR04 pulse/voltage checks, PWM/load
tests, combined-load timing, physical disconnect/recovery, flash interruptions,
repeat profile/reboot checks and heap/stack high-water measurements. Follow the
[hardware acceptance guide](../../CONTRIBUTING.md) and retain honest PASS / FAIL /
NOT TESTED results. Hardware-qualified remains false until a reviewed sign-off.
