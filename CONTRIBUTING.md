# Contributing

- Branch from `main`: `feat/<module>-<short-desc>`, `fix/...`, `docs/...`.
- Small commits, imperative messages, e.g. `config: reject duplicate pin assignment`.
- Open a PR; at least one other member reviews. Unit tests must pass.
- Keep changes inside your module; changing a header in `firmware/include/uthome/` needs team agreement.

## Module ownership (provisional, revise after professor feedback)

| Area | Folder |
|---|---|
| Runtime | `firmware/src/runtime`, `firmware/src/health` |
| Configuration | `firmware/src/config`, `schemas`, `configs`, `tools/config_validator` |
| Hardware | `firmware/hal`, `firmware/drivers`, `docs/hardware` |
| Transport / ESPHome-style integration | `firmware/src/messaging` |
| Automation | `firmware/src/automation`, `configs/scenarios` |
| Everyone | `tests/integration`, `tests/fault_injection`, demo rehearsal |
