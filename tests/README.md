# Verification

Dated test counts and build results belong in the
[evidence index](../firmware/qualification/README.md) and
[completed-work log](../docs/COMPLETED_WORK_LOG.md). Run the checks below for the
source you are changing; a previous pass does not qualify a new revision.
Hardware, wireless/static compliance and model eligibility are separate gates.

Run from the repository root. Install `tools/requirements.txt`, the pinned
formatter in `tools/c_standard/requirements.txt`, and training dependencies
for AI tests. Put GCC on PATH; Node is needed for presentation checks.

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
python -B -m unittest discover -s ai/tests -v
node --test tests/target/test_*.mjs
node --test visualiser/tests/*.test.mjs
python tools/test_uart_gpio.py
python tools/test_outputs.py
```

| Suite | What it checks |
|---|---|
| Standard | Checker behavior, pinned formatting and mandatory BARR-C policy |
| Foundation | Catalogue/board/build admission, independent instances, routing and resource conflicts |
| Runtime | Lifecycle, bound dispatch, retained cleanup, transactions and fault recovery |
| Target | Build/source guards, fake-kernel lifecycle, drivers, admission, USB/storage, telemetry, rules and UI workflow |
| Discovery | Reviewed observations/presets, ambiguity, identity limits, driver support and additive drafts |
| AI | Experiment/run validation, simulated training, deterministic evaluation and generated C parity |
| Node | Existing dashboard/discovery presentation and interaction checks |

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
owned register/wait interfaces redirected. Twenty-one isolated scenarios check
wire and timing failures, held-clock reads, final-byte NACK, FIFO bounds,
multi-chunk chip select, partial receive data and successful next transfers.
The native target build separately checks the extracted SDK boundaries.

These suites are a regression baseline, not proof that arbitrary refactoring is
safe. Review affected contracts and add focused tests before changing behavior.
Hardware timing, electrical effects, combined radio/static operation and
acceptance remain separate from this software baseline.

Native fixtures use warnings as errors and active assertions. Foundation/runtime
intentionally overlap some original C suites; runner totals are not unique C
assertion counts. Fake-kernel tests inject lifecycle failures and invoke workers
manually; they do not emulate physical scheduling. Sensor fixtures test parsing,
ownership, bounds and recovery, not electrical waveforms or measurement accuracy.

Admission tests also require GNU `objcopy` to redirect parser/vendor allocator
references to the `heaptrap` test module. Interception excludes the host CRT and
file-I/O harness. The trap records/refuses calls rather than aborting the process;
unexpected heap use produces a diagnostic and failure status. The positive
interception control and injected-failure test both complete without deliberate
crashes or Windows application-error dialogs.

C/Python parity checks decode real C-produced USB frames. UI fixtures cover
single-use review tokens, changed state/capabilities, concurrent asset requests,
unknown devices and uncertain deployment. USB is faked; no test authorizes a
physical mutation or proves actual device detection.

The [AI guide](../ai/README.md) covers its separate environment and compiler
requirements. The preserved Member 5 workflow runs independently with
`python -B -m unittest discover -s tests -v` from `legacy/member5`; it is the
legacy schema-1.0 host workflow, not the current C3 deployment path.

`ai.tests.test_loaded_model` checks the frozen diagnostic corpus, integer
classifier parity, phase boundaries, invalid input and clock reversal with a
host clock/observer double. Target qualification also checks legal build modes
and absence of model symbols in ordinary images. These software checks do not
replace the active-peripheral OFF/ON board comparison.

For real kernel execution, use the [board fixture](target/runtime_port_board/README.md).
Build and captured board outcomes live in the [evidence index](../firmware/qualification/README.md).
Physical timing, USB interruption, flash power loss, pin voltages, output safety,
concurrency and RAM/stack measurements require the [hardware procedure](../CONTRIBUTING.md).

Browser integration uses the real Configuration Desk HTML and ES modules with
mocked HTTP responses; it never opens serial devices or sends PDFs. Run
`npx playwright install chromium`, then `npm run test:browser`. The suite covers
confirmation/addition wiring, busy-state controls, review navigation, stale
responses, discovery expiry, consumed apply tokens, delayed catalogue delivery
and failed telemetry clearing. Additional scenarios cover PDF rendering/download,
stale success/failure after file changes, size/provider errors, asynchronous import
races, local-failure isolation, network/HTTP error context, datasheet generation
payloads and stale results, and telemetry/mutation serialization. The wiring-guide
tests exercise every setup, responsive focus views and saved-progress recovery.
CI installs the same pinned Chromium revision.
Artifacts stay under `build/browser-results`. To keep downloaded browser binaries
local too, set `PLAYWRIGHT_BROWSERS_PATH` to `build/playwright-browsers` for both
the install command and test run.
