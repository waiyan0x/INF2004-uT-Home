# AI contracts

The current preparation request is
[`timing-scenario.schema.json`](timing-scenario.schema.json), version 0.1.0.
It describes planned per-instance interval experiments using platform admission.
See [the timing guide](../TIMING_EXPERIMENTS.md) for the generated experiment
contract and validation. Neither contract represents measured training data or
a model prediction. Current run, dataset and timing-tree contracts are described
in [OFFLINE_PIPELINE.md](../OFFLINE_PIPELINE.md); run structure is checked by
`timing-run.schema.json`. They are explicitly separate from the older API below.

> **Legacy prototype experiment.** AI contract 0.2.0 is retained for regression
> tests only. The current preparation work begins with
> [device discovery](../../docs/DEVICE_DISCOVERY.md) and the platform catalogue;
> follow `ai/TRAINING_PIPELINE.md`. Offline experimental training is now available;
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
[PROTOTYPE_REFERENCE.md](../../configs/PROTOTYPE_REFERENCE.md). The current
[PROJECT_BASELINE.md](../../PROJECT_BASELINE.md) takes precedence for platform work.

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
node profiles from it. The platform catalogue and node schema 2.0.0 are now
independent contracts; a subsequent AI contract must map to their capabilities
and explicit instance IDs. Design that contract only after the foundation review,
then record its version and catalogue/build provenance in future datasets.
