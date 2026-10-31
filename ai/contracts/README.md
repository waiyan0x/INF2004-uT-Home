# AI contracts

The current preparation request is
[`timing-scenario.schema.json`](timing-scenario.schema.json), version 0.1.0.
It describes planned per-instance interval experiments using platform admission.
See [the timing guide](../README.md#prepare-an-experiment) for the generated experiment
contract and validation. Neither contract represents measured training data or
a model prediction. Current run, dataset and timing-tree contracts are described
in [the AI guide](../README.md); run structure is checked by
`timing-run.schema.json` for legacy/offline 0.1.0 records and
`timing-run-v0.2.schema.json` for live hardware 0.2.0 records. The live contract
adds interrupted outcomes, bounded terminal overshoot, raw-journal hashes,
retained device/build identity and exact Apply receipts. Bundle, dataset and
model versions remain 0.1.0. The recorder verifies journal/sample correlation
before bundling; schema validation alone cannot establish physical measurements.
These are explicitly separate from the older API below.

`execution-context.schema.json` adds `uthome.execution-context` 0.1.0 as a
separate sidecar. `ai/static_context.py` validates actual permanent stack
reservations, the four-candidate advice bound and reviewed static-memory
qualification evidence. It freezes `uthome.static-experiment` 0.1.0 bindings
without changing old experiment hashes or counting idle workers as model load.
Missing measurements remain null. Current branch blockers are reported by
`ai/static_readiness.py`; source scans alone do not establish compliance.

`ai/measurements.py` defines strict `uthome.workload-measurement` 0.1.0 snapshots:
at most four unique worker/source pairs, explicit availability, eleven uint32
counters matching `workcost_stats_t`, firmware/profile hashes, runtime generation
and uint64 microsecond snapshot time. Counter resets, context changes and
saturation invalidate a window. Maxima describe time since reset, not just the
delta window. Command 9 exposes the bounded workload snapshots; they are
auxiliary measurements, not new model inputs. Independent calibration and
combined wireless-load qualification remain open.

`ai/protocol_export.py` emits `uthome.protocol-c-export` 0.1.0 metadata and
`protree.c/.h` for eighteen uint32 features. It retains the original model hash,
required context hashes and disabled deployment status. The C classifier is
bounded to five node visits with no heap/recursion/writable globals; context and
profile validation remain required in its future caller.

The protocol workload study adds independently versioned 0.2.0 contracts:
`uthome.protocol-study`, `uthome.protocol-dataset`, `uthome.protocol-tree` and
`uthome.protocol-evaluation`. Their strict preparation/replay/model validators
are in `ai/protocol_study.py` and `ai/protocol_model.py`; the example input is
`ai/examples/protocol-study.json`. Eighteen input features are defined by
`ai/workload.py`. The protocol model uses its own experimental C exporter and is
intentionally incompatible with the old six-input exporter. See the
[protocol study guide](../README.md#protocol-workload-study) for metrics,
coverage/fallback behavior, fixed splits and limitations.

> **Legacy prototype experiment.** AI contract 0.2.0 is retained for regression
> tests only. The current preparation work begins with
> [device discovery](../../docs/README.md) and the platform catalogue;
> follow [the AI guide](../README.md). Offline experimental training is available;
> model deployment is not integrated. The historical interface below is not the
> platform contract.

Version **0.2.0** binds project baseline **1.0.0**. The request retains the
16-field uint32 feature vector, but payload meaning and accepted device limits
are now fixed. Requests declaring 0.1.0 are rejected rather than reinterpreted.

- `feature-request.schema.json`: structured request and queue/task-count bounds.
- `recommendation.schema.json`: recommendation/abstention structure, permitted
  device priorities, and initial confidence threshold 800/1000.
- `model-io-contract.json`: field order, encodings and baseline reference.
- `../data/sources/first-prototype-baseline.json`: versioned device features,
  policy limits, defaults, budgets and source provenance.

The superseded rationale is preserved in
[the legacy reference](../../configs/README.md#legacy-reference). The current
[platform contract](../../README.md#platform-contract) takes precedence for platform work.

## Validation

JSON Schema checks structure. `ai.uthome_ai.contracts` additionally checks
device/feature consistency, exact baseline feature constants, request bounds,
profile applicability, request correlation, fixed per-device priority, and
exact microsecond-to-millisecond conversion for BMP388/OLED/radar outputs, and
preservation of an explicit valid desired period.
Use `validate_exchange` before generating a node profile; validating a response
alone cannot establish that it matches a device or request.

The request builder copies constant device features from the baseline and
adds operating mode, desired period (0 means choose), active-device count and
queue depth. Callers cannot widen safe ranges through request metadata.
Zero means inapplicable only for the documented period/buffer fields.
For non-stream devices, `buffer_size_bytes = 0` does not mean the driver has no
internal memory; the OLED still owns its fixed framebuffer.

`payload_size_bytes` describes one device work unit: BMP register data (6),
OLED framebuffer (1024), radar wire frame (30), sonar pulse duration (4).
It never substitutes for a queue item size or serialized JSON length.

Task priority is filled deterministically (radar 8, sonar 10, BMP388 12,
OLED 16). The wrapper copies a nonzero desired period instead of predicting it.
Unsupported inputs, old versions, low confidence or invalid outputs
must become an abstention/error at the application boundary. The Python
validator raises `ContractValidationError`; it does not itself generate an
abstention response, run a model or deploy firmware.

## Status and migration

Retain 0.2.0 for historical regression tests. Do not generate new datasets or
node profiles from it. Current timing contracts are versioned independently
of this legacy API. They map explicit instance IDs to platform catalogue and
node-schema capabilities, retaining catalogue/build provenance. See the
[current pipeline](../README.md) for preparation, training and remaining work.
