# Embedded AI — discovery before training

Offline preparation has begun following the software readiness review. The
current software collects bounded I2C evidence and resolves device identity,
protocol and driver availability from reviewed catalogue facts. Follow the
[discovery design](../docs/DEVICE_DISCOVERY.md),
[TRAINING_PIPELINE.md](TRAINING_PIPELINE.md) and
[platform baseline](../PROJECT_BASELINE.md). Explicit discovery is available
through the CLI and the existing Configuration Desk. The UI now shows sourced
settings and adds reviewed suggestions to a draft without replacing existing
entries. D4 now adds radar candidates and manual OLED/GPIO confirmation in the
same flow. The new hardware paths still need physical acceptance.

The next stage has begun with [T1: timing experiment preparation](TIMING_EXPERIMENTS.md).
`python -B -m ai.timing` prepares and validates interval comparisons for a named
device instance, then exports a draft for the existing review/apply flow. It
uses platform descriptors and records version/hash provenance. Plans contain no
measurements or training labels.

[The offline pipeline](OFFLINE_PIPELINE.md) now supports simulated runs, replay
validation, dataset labels, grouped evaluation, a small trained decision tree,
guarded recommendations and checked C export. The current model is explicitly
simulation-only and does not outperform the simple rule on interval selection.
Actual measurements and physical acceptance remain pending. No UI or deployment
integration has been added.

The older model/request/recommendation files in `contracts/`, `uthome_ai/`,
`tests/test_contracts.py`, `tests/test_baseline.py` and
`data/sources/first-prototype-baseline.json` retain the historical AI 0.2.0
prototype experiment. That validator intentionally recognizes four original
models and fixed limits. It is not used by the new platform admission layer,
and must not be used to generate new deployment profiles or training data.

The active source of hardware capabilities is `catalogue/`. Generic node
validation lives in `tools/config_validator/`. The new timing-tree contract uses
reviewed capabilities, correlates recommendations to instance IDs and uses the
same admission path as manually supplied configurations.

Timing preparation and legacy regression checks remain available:

```powershell
python -m pip install -r ai/training-requirements.txt -r tools/c_standard/requirements.txt
python -B -m unittest discover -s ai/tests -v
python -B -m ai.uthome_ai.cli exchange request.json recommendation.json
```

The full tests need GCC and pinned clang-format; see the offline guide for the
isolated Windows environment. Tests train a simulated fixture and verify host C
parity. The `ai.uthome_ai.cli` command only validates the historical exchange.
Neither deploys or activates hardware.
The `ai.timing` tool uses current node admission. Discovery policy checks are
independent:

```powershell
python -B -m unittest discover -s tests/discovery -v
```
