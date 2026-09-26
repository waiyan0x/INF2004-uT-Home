# Host tools

| Path | Purpose |
|---|---|
| `configurator/` | Profile generation, validation, review, bundle verification, filesystem transfer, and localhost UI. |
| `integration_runner/` | Host scenario checks and separately attributed manual demonstration evidence. |
| `requirements.txt` | Host Python dependency for JSON Schema validation. |
| `config_validator/`, `provisioning/`, `scripts/` | Reserved scaffolding; no executable implementation lives in these directories yet. |

Run tools from the repository root with `python3 -m tools.configurator` or `python3 -m tools.integration_runner`. The active package layout and dependency direction are documented in [`docs/configuration/ARCHITECTURE.md`](../docs/configuration/ARCHITECTURE.md).
