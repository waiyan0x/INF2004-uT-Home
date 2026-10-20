# Verification

Run from the repository root. Install `tools/requirements.txt`, the pinned
formatter in `tools/c_standard/requirements.txt`, and training dependencies
for AI tests. Put GCC on PATH; Node is needed for presentation checks.

```powershell
python -B -m unittest discover -s tests/c_standard -v
python tools/c_standard/check.py
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
python -B -m unittest discover -s tests/target -v
python -B -m unittest discover -s tests/discovery -v
python -B -m unittest discover -s ai/tests -v
node --test tests/target/test_dashboard.mjs
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

Native fixtures use warnings as errors and active assertions. Foundation/runtime
intentionally overlap some original C suites; runner totals are not unique C
assertion counts. Fake-kernel tests inject lifecycle failures and invoke workers
manually; they do not emulate physical scheduling. Sensor fixtures test parsing,
ownership, bounds and recovery, not electrical waveforms or measurement accuracy.

C/Python parity checks decode real C-produced USB frames. UI fixtures cover
single-use review tokens, changed state/capabilities, concurrent asset requests,
unknown devices and uncertain deployment. USB is faked; no test authorizes a
physical mutation or proves actual device detection.

The [AI guide](../ai/README.md) covers its separate environment and compiler
requirements. The preserved Member 5 workflow runs independently with
`python -B -m unittest discover -s tests -v` from `legacy/member5`; it is the
legacy schema-1.0 host workflow, not the current C3 deployment path.

For real kernel execution, use the [board fixture](target/runtime_port_board/README.md).
Build and captured board outcomes live in the [evidence index](../firmware/qualification/README.md).
Physical timing, USB interruption, flash power loss, pin voltages, output safety,
concurrency and RAM/stack measurements require the [hardware procedure](../CONTRIBUTING.md).
