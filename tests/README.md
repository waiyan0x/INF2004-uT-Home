# Verification

This is the assessment entry point for the final FR1-FR5 and NFR1-NFR5 in
`AAI13_PreTri2.pdf`. Read the requirement map, then the case catalogue and the
reproduction commands. Each case states the stimulus, expected observable result
and executable evidence. The tests run project code; doubles replace only the
boundaries identified below.

## Reading the results

The repository is the submission. Open [GitHub Actions](https://github.com/waiyan0x/INF2004-uT-Home/actions/workflows/c-standard.yml),
select the run for the submitted commit, and read **Submission test evidence**
in the host job summary. It reports each suite's actual outcome. Download that
run's `host-test-evidence-<run-id>` artifact for named tests, counts, failures and
browser diagnostics. Logs are retained for 90 days; the test source and commands
remain in Git. ESP32-C3 build artifacts are separate and retained for 14 days.

A failed, skipped or not-run check is not a pass. Python totals count test methods,
not every generated input or C assertion; some suites deliberately reuse C
fixtures. The finite-domain counts below describe the exact input ranges, not
line coverage or proof of all possible interleavings. No line/branch coverage
percentage is claimed.

Evidence has three distinct levels:

- **Host execution:** actual C/Python/JavaScript code, assertions and controlled
  faults. The TLS integration tests use a real authenticated loopback broker,
  certificate verification, the real C transaction owner/HALs and simulated buses.
  The HA observer checks published discovery/state; it is not a running HA frontend.
- **Target build:** the pinned compiler, SDK, kernel, link map, source identities
  and structural memory checks. Compilation does not execute board tests.
- **Physical qualification:** image-specific electrical, timing, radio, persistence
  and rendered-display observations. See the [physical summary](../tools/configurator/README.md#physical-verification),
  [AAI13 assessment](../docs/reports/wifi-static-memory/README.md) and
  [hardware acceptance procedure](../HARDWARE_ACCEPTANCE.md). A completed classroom
  demonstration is not automatically a pass for unrecorded fault scenarios.

## Requirements and test cases

Requirement wording is condensed from the final submitted specification.

| Requirement | Observable acceptance criterion | Cases below |
|---|---|---|
| FR1: reviewed Markdown selection and preview | Valid descriptions show bus settings, counts and timeout; preview performs no peripheral operation. | T01-T02 |
| FR2: load, execute and switch without reflashing | Loading performs no bus I/O. Explicit execution uses the acknowledged active rule. BMP388 -> LD2450 -> BMP388 works on one owner/image. | T02-T04 |
| FR3: I2C write/read, SPI write, UART TX/RX | Admit supported shapes within TX 30, RX 32 and 10-1,000 ms; reject unsupported SPI reads and invalid settings. | T05-T08 |
| FR4: complete result and six read-only HA entities | Preserve identity, counts, original completion time and raw bytes; publish six distinct read-only entities without another bus call. | T03-T04, T09 |
| FR5: USB profile lifecycle and uncertain-result recovery | Validate/apply/save/restore with rollback and retained cleanup; reconnect queries identity before exact cached recovery. | T10-T13 |
| NFR1: fixed 96-byte authenticated TLS exchange and one owner | Reject malformed/cross-session replies, invalid trust/login and a second journal owner; never queue a duplicate action. | T02, T14-T16 |
| NFR2: rejection, exact retries, idle-plan requirement and cancellation | Invalid/stale commands preserve the rule and perform no bus I/O; retry does not repeat writes; loss retains ownership until successful cleanup. | T05-T08, T13, T16-T19 |
| NFR3: bounded RAM, startup objects and scoped no-heap policy | Reject capacity exhaustion, retain/reuse fixed resources, and fail budget/creation checks. Physical radio allocations require the separate measured exemption. | T20-T23 |
| NFR4: unavailable failed/unknown/offline/expired data | Zero bytes remain valid; results are unavailable at age >= 5,000 ms, measured from original completion, including cached recovery. | T09, T13, T24-T25 |
| NFR5: BARR-C:2018 and approved deviations | Pinned gate, checker regressions, manual review and affected warning-as-error tests all apply. No new exception follows from a passing test. | T26 |

### Normal operation and data contracts

| Case | Stimulus and expected result | Executable evidence |
|---|---|---|
| T01 | Compile every reviewed description; reject missing/ambiguous blocks, wrong types, unknown operations, invalid provenance and altered PDF hashes. Prose cannot override executable fields. Rejection preserves an existing output file. | [Description cases](target/test_descriptions.py), especially `test_markdown_prose_cannot_override_executable_fields` and `test_cli_checks_pdf_hash_and_does_not_replace_output_on_rejection` |
| T02 | Preview, load and execute separately. Selection alone sends no action; load does no bus I/O; changing preview leaves execution bound to the acknowledged rule. A busy action cannot be queued twice. | [Controller](target/test_transaction_controller.py), [Desk service](target/test_configurator_runtime.py), [real browser controls](browser/runtime.spec.mjs) |
| T03 | Switch I2C -> SPI -> UART -> I2C through Markdown, TLS and HA publication on the same compiled C owner. Each load leaves bus counters unchanged. Executes return `50`, SPI TX=1/RX=0, UART bytes `00` through `1F`, then `50`; boot identity stays unchanged. | [Display integration](target/test_homeassistant.py): `test_i2c_spi_uart_i2c_switch_reaches_display_on_one_compiled_owner` |
| T04 | Send maximum UART TX=30 and RX=32 through TLS. All 32 bytes, including zero, and exact transfer counts survive the round trip. No transmit occurs at load. | [TLS integration](target/test_transaction_mqtt.py): `test_uart_maximum_transmit_and_receive_survive_tls_round_trip` |
| T05 | Sweep every TX/RX byte pair for each protocol. Admit only nonempty supported shapes; reject over-capacity lengths and SPI receive without changing the preceding rule or calling the bus. | [Finite domains](target/test_txjob_domains.py): `test_every_tx_rx_length_pair_for_each_protocol` |
| T06 | Sweep protocol/address bytes, SPI pin pairs/modes/control levels, timeout values, unused-byte values and one-character identifier bytes. Rejections preserve the complete active identity and bus settings. | [Finite domains](target/test_txjob_domains.py); exact ranges in the next section |
| T07 | Test each protocol's minimum/maximum rate and adjacent invalid values; identifier length, reserved fields, counts and driver-reported inconsistencies. Rejected updates preserve the last valid rule; malformed driver results clear RX and allow recovery. | [Decision engine](target/test_txjob.py), [host/C codec parity](target/test_transaction_client.py) |
| T08 | Exercise I2C NACK/arbitration/held-clock/FIFO faults, SPI stalls and chip-select lifetime, partial UART receive and TX drain. Failures release or retain resources as specified; a successful next transfer remains possible. | [Bus recovery](target/test_bus_recovery.py), [real HAL owner](target/test_tx_owner.py), [UART lifecycle](target/test_uart_startup.py) |
| T09 | Verify six stable, distinct HA entities with no command channel; updates are not readings. State is non-retained, discovery is retained, and HA birth republishes discovery without I/O. | [HA projection and broker cases](target/test_homeassistant.py): `test_discovery_is_stable_bounded_and_has_no_command_channel`, `test_markdown_switch_updates_display_without_another_driver_call` |

### Invalid input, recovery and freshness

| Case | Stimulus and expected result | Executable evidence |
|---|---|---|
| T10 | Apply valid/invalid profiles, disabled-device conflicts, duplicate names/addresses, routing and resource limits. Host and independent C admission reject invalid candidates. Failed activation rolls back; failed rollback enters explicit recovery. | [Foundation](foundation/test_foundation.py), [target admission](target/test_admission.py), [C transaction fault fixture](fault_injection/rttrans_main.c) |
| T11 | Corrupt every partition-table byte; inject table/read/write/erase failures and interrupted saves. No write escapes the two reviewed slots; bounds fail before flash I/O; an uncertain SAVE is not silently retried. | [Storage layout](target/test_storage_layout.py), [storage adapter](target/test_storage_adapter.py), [configuration wire/persistence](target/test_config_transfer.py), [host mutation contracts](target/test_refactor_contracts.py) |
| T12 | Lose/truncate/corrupt USB replies, fragment frames and inject unrelated traffic. Reconnect chooses the next ID; uncertain APPLY is not resent. Changed board/capabilities or consumed review token prevents deployment. | [USB client](target/test_usb_client.py), [Desk validation/deployment](target/test_configurator.py) |
| T13 | Lose an SPI-write reply after actual execution, close/reopen the host journal, query and recover. Cached success reports TX=1 while the SPI execution counter remains exactly one. If completion is not known, the host keeps an unknown outcome. | [Real TLS lost-write case](target/test_transaction_mqtt.py): `test_lost_spi_write_reply_is_recovered_without_repeating_the_write`; [controller recovery cases](target/test_transaction_controller.py) |
| T14 | Reject wrong hostname, untrusted CA and wrong broker login. A subsequent correctly authenticated connection succeeds. Trust rejection causes no peripheral I/O. | [TLS connection cases](target/test_transaction_mqtt.py) |
| T15 | Compile 96-byte commands and reject wrong version, length, padding, identity, kind, generation and delayed replies. A transport only completes on a matching response. | [Codec and filtering](target/test_transaction_client.py), [malformed engine requests/queries](target/test_txjob.py), [MQTT parser](target/test_network_services.py) |
| T16 | Attempt a second journal owner/process, corrupt durable state, exhaust IDs/revisions, or fail journal writes before/after publication. No wrapped command is sent; committed pending bytes survive a crash. | [Durable controller cases](target/test_transaction_controller.py) |
| T17 | Retry exact last bytes, modify the same ID, send older IDs, change plan generation, or reboot with the same generation. Only an exact same-session retry returns the byte-identical cached reply; no duplicate I/O. | [Decision engine](target/test_txjob.py): retry, generation, boot and ID-exhaustion cases |
| T18 | Run with a non-idle plan, lose the link during UART work, time out at the deadline, reverse the clock, or fail acquisition/cleanup. New work stays blocked while cleanup owns resources; reconnect never restarts the cancelled transfer. | [Owner cancellation](target/test_tx_owner.py), [deadline/acquisition/cleanup tests](target/test_txjob.py) |
| T19 | Inject stale wakes, early timeouts, ISR/context errors, full notifications and waiter reuse. Waiting remains within one absolute budget and does not recreate startup resources. | [Network event adapter](target/test_event_adapter.py), [fixed queue pool](target/test_network_pool.py) |
| T24 | Observe zero-valued data at ages 0, 4,999, 5,000 and 5,001 ms. Data is valid before 5,000 and unavailable at/after it. Querying or recovering does not reset completion time. | [Controller](target/test_transaction_controller.py): `test_five_second_expiry_boundary_uses_original_esp_completion`, `test_recovered_result_retains_original_completion_time_and_age` |
| T25 | Fail a read, disconnect, lose a reply, change boot/generation, or deliver a delayed browser snapshot. Desk and HA clear current bytes rather than republishing old success; active-rule identity may remain separately available. | [HA states](target/test_homeassistant.py), [Desk service](target/test_configurator_runtime.py), [Chromium transitions](browser/runtime.spec.mjs) |

### Resource limits and coding policy

| Case | Stimulus and expected result | Executable evidence |
|---|---|---|
| T20 | Fill all 512 JSON slots, exceed by one, repeat rejection/reuse 20 times; accept 8,192-byte input and reject 8,193. Positive controls prove allocator interception is live. | [Admission parser tests](target/test_admission.py) |
| T21 | Exhaust queue, TLS record, hostname, certificate, task-stack and native-record storage; reject invalid releases and reuse slots without new runtime kernel objects. Clear sensitive storage on release. | [Queue pool](target/test_network_pool.py), [TLS storage](target/test_tls_storage.py), [TLS output/state/crypto storage](target/test_tls_output.py), [lwIP records](target/test_lwip_records.py), [kernel task tests](kernel_tasks), [runtime port](target/test_runtime_port.py) |
| T22 | Fail each startup creation position and attempt reinitialization. No partially initialized owner becomes available; startup is not retried lazily. Creation audits count attempts even when objects are later deleted. | [GPIO startup](target/test_gpio_startup.py), [UART startup](target/test_uart_startup.py), [creation audit](target/test_uart_audit.py), [memory audit](target/test_memory_audit.py) |
| T23 | Omit/duplicate RAM attribution, overrun a module or total cap, or alter build pins/source/allocator scope. The qualification gate fails instead of accepting missing evidence. The linked-budget tests do not measure live Wi-Fi heap. | [Memory accounting](target/test_memory_budget.py), [build qualification](target/test_qualification.py); [physical NFR3 evidence and limits](../docs/reports/wifi-static-memory/README.md) |
| T26 | Run the pinned formatter/checker and its malformed-source regressions; compile C fixtures with warnings as errors and assertions enabled. Check approved deviation anchors and complete the manual review for changed C. | [Checker tests](c_standard/test_check.py), [project policy and manual checklist](../C_CODING_STANDARD.md), [scoped approvals](../tools/c_standard/approval-20260928.json) |

## What is exhaustive

The six tests in [test_txjob_domains.py](target/test_txjob_domains.py) execute
**340,183 candidate loads**, plus six initial valid loads, against the real C
engine. Every candidate checks status, active identity, bus settings, empty load
result and zero bus-call counters. Expected ranges come from the wire contract;
tests do not call the host validator to decide whether the C validator is right.

| Finite domain | Candidates |
|---|---:|
| All 256 protocol bytes and all 256 address bytes for each supported protocol | 1,024 |
| All 256 x 256 TX/RX length-byte pairs for each of three protocols | 196,608 |
| All 256 x 256 SPI CS/DC pin pairs, 256 modes, and 256 levels with present/absent DC | 66,304 |
| Every unsigned 16-bit timeout (0 through 65,535) | 65,536 |
| All 255 nonzero values at each of 41 reserved/unused update positions | 10,455 |
| Every possible single-character identifier byte, including NUL and non-ASCII | 256 |

Other tests cover boundaries and distinct behaviors: 32/64-bit identity limits,
rate limits, identifier lengths, parsing errors, every enumerated startup fault,
cleanup, retries, restart sequences, and actual maximum-length data transfer.
The sweeps do **not** enumerate the Cartesian product of every field, every
payload, every concurrent schedule or every physical failure. Optional hardware,
visible OLED output after idle, arbitrary certificate chains, forced concurrent
USB parsing/TLS verification and long-duration radio behavior retain the limits
recorded in the hardware/resource guides.

## Reproduce the requirement-focused checks

From the repository root, install the dependencies described below and put GCC
on PATH. This command runs the relevant C decision/HAL, description, controller,
real TLS and HA-contract tests without a board, COM port or production broker:

```powershell
$env:PYTHONPATH = 'tests/target;.'
python -B -m unittest test_descriptions test_txjob test_txjob_domains test_tx_owner test_transaction_client test_transaction_controller test_transaction_mqtt test_homeassistant test_configurator_runtime -v
```

On Linux/macOS, use `PYTHONPATH=tests/target:.` before the Python command instead.
For browser-visible controls, run `npm run test:browser -- tests/browser/runtime.spec.mjs`.
Expected result: process exit code 0, every selected test marked `ok`/passed, no
unexplained skips. The finite sweeps report six Python tests, not 340,183 methods;
a failure names the offending input. UART bytes and I2C values in these checks
are deliberate simulated inputs, not new sensor measurements.

## Running the full suite

Keep fresh local logs under ignored `build/`; a previous pass does not qualify
a new revision. CI runs the suites below and preserves the current run's evidence.

Run from the repository root. Install `tools/requirements.txt`, the pinned
formatter in `tools/c_standard/requirements.txt`. Put GCC on PATH; Node is
needed for presentation checks. The transaction controller's local TLS MQTT
tests also require `tools/transaction/requirements-dev.txt` (installed by CI).
They start a loopback-only broker with temporary credentials/certificates and
the compiled C owner with simulated buses; they do not contact equipment.

`test_homeassistant` verifies the display adapter's public CLI option and MQTT
messages using that same C owner and broker. It covers Markdown A/B/A switching,
zero data, failure/expiry, retained discovery, non-retained state, actual last
will, host/broker restarts and HA birth without additional driver calls. These
checks do not run a Home Assistant instance or qualify its rendered display.

The pinned JavaScript development checks use `package-lock.json`. With Node
22.13 or later in the 22.x line, or Node 24+, run `npm ci --ignore-scripts` and
`npm run lint`. `npm test` runs the same dashboard and visualiser Node suites
listed below. Browser/runtime use still needs no npm install. CI also runs
the [strict host data-contract check](../tools/config_transfer/README.md).

```powershell
python -B -m unittest discover -s tests/c_standard -v
python tools/c_standard/check.py
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
python -B -m unittest discover -s tests/target -v
python -B -m unittest discover -s tests/discovery -v
node --test tests/target/test_*.mjs
node --test tools/wiring_visualiser/tests/*.test.mjs
python tools/test_uart_gpio.py
```

| Suite | What it checks |
|---|---|
| Standard | Checker behavior, pinned formatting and mandatory BARR-C policy |
| Foundation | Catalogue/board/build admission, independent instances, routing and resource conflicts |
| Runtime | Lifecycle, bound dispatch, retained cleanup, transactions and fault recovery |
| Target | Build/source guards, fake-kernel lifecycle, drivers, admission, USB/storage, telemetry, rules, durable host controller, local TLS MQTT and UI workflow |
| Discovery | Reviewed observations/presets, ambiguity, identity limits, driver support and additive drafts |
| Node | Existing dashboard/discovery presentation and interaction checks |
| Buddy 1 | Static task-pool lifecycle and shim checks; run by CI on Linux with address and undefined-behavior sanitizers |

For the Buddy 1 tests, initialize the pinned kernel submodule first. On Linux,
run `python -m unittest discover -s tests/kernel_tasks -p test_fixed_stack_pool.py -v`,
then each `tests/kernel_tasks/test_shim_*.py`, `test_c3task_static_pool.py`,
`test_c3task_alignment.py`, `test_c3task_no_static.py`, and
`test_fixed_stack_stress.py` with Python. Three
fixtures require a compiler with AddressSanitizer and UndefinedBehaviorSanitizer;
their existing leak-detection exclusion is not evidence of leak freedom. These
host checks do not establish on-board scheduling or memory safety.

The target suite also protects the host configuration mutation boundary: SAVE
uses the active revision, invalidates outstanding reviews and never silently
retries an uncertain SAVE or RECOVER. Exhausted sequence/revision values and an
absent active configuration must not send a mutation. Dashboard and visualiser
tests run in CI as well as locally.
The target Node glob includes the configurator polling/review race regressions
as well as dashboard presentation checks.
Storage adapter tests compile the same `cfgslot` core used by the target and
inject size/table/read/write/erase failures through synchronous callbacks. They
check complete table coverage, internal scratch chunking, overflow rejection and
peer-slot isolation; SDK execution and physical flash behavior require target
qualification separately.

UART lifecycle tests compile the real `c3uart` and `uartio` against the owned
`ufake` native-call boundary. They record calls and inject failures without
vendor header doubles. Each case loads fresh boot state; repeated startup and
50 open/close cycles check permanent resource ownership. GPIO startup tests run
the shared `bootonce` core for both fixed capacities and every partial failure.
Text checks remain only for structural restrictions and application boot order.

I2C/SPI recovery tests similarly execute the real transfer adapters, with only
owned register/wait interfaces redirected. Twenty-three isolated scenarios check
wire and timing failures, held-clock reads, final-byte NACK, FIFO bounds,
multi-chunk chip select, partial receive data and successful next transfers.
Delay cases distinguish clock errors, closed sessions and exhausted budgets.
The native target build separately checks the extracted SDK boundaries.

Event-adapter tests compile the real `c3event`, `netevent` and `netwait` modules.
Controlled `c3ewait` responses cover partial startup, full/broken notifications,
stale wakes, early timeouts, clock/wait errors, waiter reuse, deletion and context
rejection. Python checks observable results and call budgets; native SDK mapping
and cache-check IRAM placement are separate real-target build gates.

Runtime-rule tests in `target/test_txjob.py` exercise each protocol's rate limits,
timeout and transfer-size boundaries, I2C addresses, SPI settings, and every
reserved or unused request byte. Rejected updates preserve the active revision
and perform no bus I/O. Driver-count errors clear returned data and permit a
subsequent valid transaction; deadline and reversed-clock cases check poll/close
counts. Failed acquisition retains ownership until cleanup succeeds, without
repeating acquisition or polling the failed transfer.

These suites are a regression baseline, not proof that arbitrary refactoring is
safe. Review affected contracts and add focused tests before changing behavior.
Hardware timing, electrical effects, combined radio/static operation and
acceptance remain separate from this software baseline.

Native fixtures use warnings as errors and active assertions. Foundation/runtime
intentionally overlap some original C suites; runner totals are not unique C
assertion counts. Fake-kernel tests inject lifecycle failures and invoke workers
manually; they do not emulate physical scheduling. Sensor fixtures test parsing,
ownership, bounds and recovery, not electrical waveforms or measurement accuracy.

Admission tests also require GNU `objcopy` or `llvm-objcopy` on PATH to redirect
parser/vendor allocator references to the `heaptrap` test module. macOS Mach-O
C symbols use their leading underscore; other supported hosts use ELF-style
names. The positive interception control and injected heap-violation test are
required on both paths. Interception excludes the host CRT and
file-I/O harness. The trap records/refuses calls rather than aborting the process;
unexpected heap use produces a diagnostic and failure status. The positive
interception control and injected-failure test both complete without deliberate
crashes or Windows application-error dialogs.

C/Python parity checks decode real C-produced USB frames. UI fixtures cover
single-use review tokens, changed state/capabilities, concurrent asset requests,
unknown devices and uncertain deployment. USB is faked; no test authorizes a
physical mutation or proves actual device detection.

The cleanup review of the affected C fixtures checked the board entry's build
selection, retained startup and ownership paths, callback types, initialization,
integer and pointer bounds, and failure handling against the manual BARR-C
checklist. Retired timing-model and optional-output branches were removed with
their callers; the remaining radar and sonar checks keep active assertions and
warnings as errors. This review does not replace target or hardware acceptance.

For real kernel execution, use the [board fixture](target/runtime_port_board/README.md).
Keep build and captured board outcomes together with their exact source hashes.
Physical timing, USB interruption, flash power loss, pin voltages, output safety,
concurrency and RAM/stack measurements require the [hardware procedure](../HARDWARE_ACCEPTANCE.md).

Browser integration uses the real Configuration Desk HTML and ES modules with
mocked HTTP responses; it never opens serial devices or sends PDFs. Run
`npx playwright install chromium`, then `npm run test:browser`. The suite covers
confirmation/addition wiring, busy-state controls, review navigation, stale
responses, discovery expiry, consumed apply tokens, delayed catalogue delivery
and failed telemetry clearing. Additional scenarios cover PDF rendering/download,
stale success/failure after file changes, size/provider errors, asynchronous import
races, local-failure isolation, network/HTTP error context, catalogue draft generation
payloads and stale results, and telemetry/mutation serialization. The wiring-guide
tests exercise every setup, responsive focus views and saved-progress recovery.
CI installs the same pinned Chromium revision.
Artifacts stay under `build/browser-results`. To keep downloaded browser binaries
local too, set `PLAYWRIGHT_BROWSERS_PATH` to `build/playwright-browsers` for both
the install command and test run.

Networking-only software checks: `python -B -m unittest discover -s tests/target
-p "test_network*.py"`. These include actual C protocol/diagnostic code and PC
client/size-tool tests with explicit socket/BLE doubles. They contact no real
broker or board and do not establish hardware acceptance. See the
[network acceptance matrix](../firmware/src/network/README.md#remaining-acceptance).

The separate [TLS record compatibility probe](../tools/target/README.md#tls-record-compatibility-probe)
builds the pinned SDK's TLS library on the host and runs real encrypted loopback
MQTT, including valid/rejected certificates and a 96-byte round trip. It needs the
SDK checkout, so it is an explicit experiment outside the ordinary unit suite.
It does not qualify target memory, simultaneous sessions or physical hardware.
