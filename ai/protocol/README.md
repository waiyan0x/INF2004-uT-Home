# Protocol timing study

This package owns the eighteen-input protocol pipeline. The six-input model
remains in `ai/model.py`; shared recording, measurements and provenance remain
in the parent package. Follow the [AI guide](../README.md) for study design,
capture requirements, commands and qualification limits.

| Module | Responsibility |
|---|---|
| `study` | Freeze workload families and derive labels from verified journals |
| `capture` | Collect planned trials with restoration and no automatic retries |
| `resume` | Verify prior batches and identify missing trials |
| `model` | Train, evaluate and make experimental predictions |
| `export` | Produce bounded C and check host/target parity |
| `gate` | Compare against the frozen deployment-benefit criteria |
| `baseline` | Replay independently frozen timing-rule calibration |

Each module has a CLI: `python -B -m ai.protocol.<module> --help`.
The old `python -B -m ai.protocol_capture` command remains a forwarding entry
point for retained local capture scripts. Current callers import this package.

New study preparation records the current implementation paths and hashes.
Frozen studies still require their original source snapshots for replay; never
rewrite historical qualification records or hashes to follow a source move.
