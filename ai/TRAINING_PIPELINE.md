# Embedded AI pipeline — paused at the foundation gate

The reusable platform baseline is [PROJECT_BASELINE.md](../PROJECT_BASELINE.md).
Establish and review that foundation before generating datasets or training.
The existing AI contract 0.2.0 and policy file describe a historical experiment
limited to the original prototype devices. They are retained for reproducibility;
they are not the new platform contract and must not drive deployment generation.

## Foundation requirements before resuming

- Reviewed device descriptors, board capabilities and actual build availability.
- Generic node schema and per-instance driver/context ownership.
- Multiple hardware arrangements accepted by the same runtime.
- Independent instances and reconfiguration, with invalid candidates preserving
  the working configuration.
- A defined target path for real driver activation, queues and rollback.

Host acceptance code is in `tests/foundation/`. Target integration and board
qualification remain separate work. A passing legacy AI test is not this gate.

## Direction for the next AI contract

Design and version a new capability-based contract after reviewing the foundation.
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

## Training after the gate

1. Define the recommendation objective and demonstrate that useful choices remain
   after deterministic constraints/defaults are applied.
2. Establish rules as a reference and collect labelled scenarios with provenance.
   Synthetic policy labels are not hardware measurements or proof of optimality.
3. Version the dataset with descriptor, board, build and AI contract revisions.
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
bounded inference and cannot bypass platform admission. No dataset or model is
created by the foundation work.
