# Embedded AI pipeline — discovery and device specifics first

Updated 2026-10-13. The reusable platform baseline is
[PROJECT_BASELINE.md](../PROJECT_BASELINE.md). The software readiness review
supports offline preparation; full hardware acceptance remains incomplete.
The current priority is detecting supported connected devices and presenting
their protocol and settings. Follow the
[device discovery contract and checkpoints](../docs/DEVICE_DISCOVERY.md)
for device specifics. With equipment unavailable, the user has authorized the
[offline simulation/training pipeline](OFFLINE_PIPELINE.md) while physical
discovery acceptance remains pending. Simulated data is never hardware evidence.

The existing AI contract 0.2.0 and policy file describe a historical experiment
limited to the original prototype devices. They are retained for reproducibility;
they are not the new platform contract and must not drive deployment generation.

## Foundation retained from the platform

- Reviewed device descriptors, board capabilities and actual build availability.
- Generic node schema and per-instance driver/context ownership.
- Multiple hardware arrangements accepted by the same runtime.
- Independent instances and reconfiguration, with invalid candidates preserving
  the working configuration.
- A defined target path for real driver activation, queues and rollback.

Host acceptance code is in `tests/foundation/`. Target integration and board
qualification remain separate work. A passing legacy AI test is not this gate.

## Current sequence

1. **D1: contracts and catalogue policies.** Define connection evidence, identity,
   capability lookup and reviewed recommendations. Implemented as reviewed data
   and the shared workflow design.
2. **D2: generic I2C discovery.** Implemented in software: a reviewed preset,
   bounded register observations, USB exchange and PC catalogue matching.
   BMP388 is the first identity rule; unknown, busy, failed and unsupported
   results remain explicit. Physical acceptance of this new path is pending.
3. **D3: local review.** Implemented in the existing Configuration Desk: show
   protocol and sourced settings, add without replacing draft entries, then use
   the existing node validator and review/apply path. Host/browser checks use
   simulated board responses; physical discovery acceptance remains pending.
4. **D4: radar and manual devices.** Implemented in software: valid radar reports
   form candidates requiring confirmation; OLED/GPIO use explicit manual
   confirmation. The existing UI and separate review/apply path are preserved.
   Physical discovery acceptance remains pending.
5. **T1: timing experiment preparation.** Implemented offline. Prepare per-instance
   interval comparisons against current/default settings, validate complete
   profiles and record source versions/hashes. Export is a draft only; plans
   contain no measurements or labels. See [TIMING_EXPERIMENTS.md](TIMING_EXPERIMENTS.md).
6. **T2: run records and replay.** Implemented offline: correlated, bounded run
   records; interrupted/incomplete-run rejection; separate simulation/hardware
   origins. Actual USB capture and evidence collection still require a later step.
7. **T3: offline dataset and model.** Implemented: capability-based timing features,
   complete-repetition labels, family splits, a small trained decision tree,
   rules/default comparisons and non-deployable recommendations. The synthetic
   rehearsal verifies plumbing, not real-world prediction benefit.
8. **T4: C preparation.** Implemented: bounded integer export, BARR-C checks,
   Python/C parity and standalone C3 object compilation. Firmware integration,
   measured retraining and on-board timing/resource acceptance remain pending.

Detection and protocol selection are deterministic. Supply voltage, pins,
identity, driver availability and supported commands are never model guesses.
Start any later model with reading/refresh interval recommendations only;
timeouts, buffer sizes, priorities and resource allocation stay under reviewed
rules until separately justified. There must be a measurable benefit over the
rules before deploying a trained model.

## Direction for the next AI contract

The experimental timing-tree contract 0.1.0 is documented in
[OFFLINE_PIPELINE.md](OFFLINE_PIPELINE.md). It is separate from the old prototype
API. Its current scope is candidate interval classification and abstention.
Resolve trusted specifications from `catalogue/devices/`; obtain resource limits
and driver availability from the selected board/build. Keep device identity for
correlation and constraint lookup, without making a four-model enum the model's
entire scope. Recommendations map to an explicit instance ID.

Candidate inputs are supported operations, data production pattern, permitted
rate/timeout ranges, payload/work cost, user latency or power goals and available
resources. Distinguish measured values from estimated/specification values. Avoid
features that simply reveal an answer already fixed by the selected descriptor.

AI may recommend tunable settings within validated capabilities. It must not
invent protocol commands, claim an unavailable driver, change electrical rules,
allocate conflicting pins or expand board/device limits. A recommendation follows
the same shared configuration validation as a manually written profile.

## Training after discovery and recommendation review

1. Define the recommendation objective and demonstrate that useful choices remain
   after deterministic constraints/defaults are applied.
2. Establish rules as a reference and collect labelled scenarios with provenance.
   Synthetic policy labels are not hardware measurements or proof of optimality.
3. Version the dataset with discovery policy, descriptor, board, build and model
   contract revisions. Retain observation method and identity basis. Do not use
   synthetic matches or user confirmation as measured detection accuracy.
4. Split by scenario families and evaluate held-out device/capability combinations
   where the model's supported domain permits; report unsupported cases separately.
5. Start with a small decision tree exported as C. Compare learned performance
   against rules before adding an integer neural runtime.
6. Check host/C parity, invalid-output rejection, abstention and downstream
   configuration validity. Estimate resources, then measure on the board.
7. Integrate with the existing local configurator after explicit instance mapping
   and independent firmware validation are in place.

Keep the provisional AI targets (32 KiB model flash, 16 KiB total module RAM and
10 ms observed loaded latency) as budgets to verify, not measured achievements.
The PC owns preparation/training/evaluation. The eventual board worker performs
bounded inference and cannot bypass platform admission. The offline pipeline now
creates explicitly simulated datasets/models; the current result gives no reason
to replace the simple rule. Hardware measurements and acceptance
remain required before claiming target latency, resource use or reliability.
