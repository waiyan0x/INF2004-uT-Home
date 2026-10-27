# Verification

The [static-host build experiment](../firmware/qualification/esp32c3-static-host-build-20261027.json)
adds effective-setting/isolation checks: **183 target tests**, the unchanged
**283-file** pinned C gate and fresh ordinary/diagnostic builds. Its static-host
map passes the unchanged limits, at 251,813 bytes total; selecting static host
globals alone is not a no-malloc claim. The preceding
[GATT software checkpoint](../firmware/qualification/esp32c3-gatt-final-software-20261027.json)
adds two isolation tests: **182 target tests**, the **283-file** pinned C gate,
fresh isolated GATT and ordinary application builds, unchanged aggregate memory
limits and absence of radio/model entry points in the ordinary image. It does
not claim RF execution or static-compliant BLE. The preceding
[software regression](../firmware/qualification/system-software-regression-20261027.json)
records 180 target/host tests, 119 AI tests, the full 281-file C standard gate,
and fresh application/fixture builds. It includes command-9 version-1/version-2
C/Python parity, scheduler window validation, mixed network queue capacities,
the checked-in three-protocol profile and the Windows HTTP rejection fix.
Policy, foundation, runtime, discovery and existing dashboard checks also passed.
Earlier failures and their fixes are retained in the record; assertions,
warnings-as-errors, formatter and time limits were not weakened. Hardware,
wireless/static compliance and model eligibility remain separately qualified.

The [27 October allocation-audit checkpoint](../firmware/qualification/esp32c3-allocation-audit-20261027.json)
records 128 passing target/host tests, 90 AI tests, the 237-file C standard gate,
fresh application/fixture builds and two 909-check fixture boots. Version-3
telemetry parity includes application-startup counters, legacy captures and
maximum fragmented responses. Stack, wireless and model-release qualification
remain separate.

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

For real kernel execution, use the [board fixture](target/runtime_port_board/README.md).
Build and captured board outcomes live in the [evidence index](../firmware/qualification/README.md).
Physical timing, USB interruption, flash power loss, pin voltages, output safety,
concurrency and RAM/stack measurements require the [hardware procedure](../CONTRIBUTING.md).
