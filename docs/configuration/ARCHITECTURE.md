# Configuration and integration architecture

The Member 5 host code is organized around one profile contract. The browser and CLI call the same Python functions; no UI path can bypass validation before approval or packaging.

```text
configs/recommendations/ ──► generator ──► candidate profile
configs/nodes/ ──────────────────────────► candidate profile
                                             │
                                             ▼
schemas/profile.schema.json ───────────► validator
                                             │
                                             ▼
                                       review decision
                                             │ profile hash
                                             ▼
                                      verified bundle
                                             │
                           ┌─────────────────┴──────────────────┐
                           ▼                                    ▼
                    filesystem adapter                future ESP32-S3 adapter
                    (host simulation)                (firmware protocol needed)

configs/health/ or imported snapshot ──► monitoring validator ──► UI
configs/scenarios/ + evidence ──────────► integration runner ───► report
```

## Folder ownership

| Folder | Contract or responsibility |
|---|---|
| `schemas/` | Versioned profile shape. Firmware and host must agree before activation. |
| `configs/nodes/`, `configs/invalid/` | Valid and rejected profile fixtures. |
| `configs/recommendations/` | Structured recommendation inputs; no arbitrary generated JSON. |
| `configs/health/`, `configs/scenarios/` | Monitoring samples and demonstration definitions. |
| `tools/configurator/generator.py`, `validator.py` | Pure profile construction and safety checks. |
| `tools/configurator/workflow.py`, `serialization.py` | Review state and a digest of the exact approved profile. |
| `tools/configurator/bundle.py` | Transactional bundle creation, strict verification, and the filesystem adapter. |
| `tools/configurator/web.py`, `static/` | Local HTTP boundary and browser presentation. |
| `tools/integration_runner/` | Host checks plus clearly labeled human observations. |
| `tests/unit/`, `tests/integration/` | Contract and workflow regression tests. |

## Reuse rules

- Call `validate_profile` at every profile input boundary. It runs JSON Schema first, then cross-field checks.
- Use `profile_sha256` for both approval and bundle comparison. A changed profile needs a new review.
- Use `verify_bundle` before any transfer adapter reads bundle files. It checks names, hashes, metadata, and profile validity.
- Treat health snapshots as untrusted. `validate_snapshot` is the only path into the UI's health state.
- Keep transport code behind the bundle boundary. A copied file proves `transferred`; only a digest-bearing firmware acknowledgement can prove `stored` or `activated`.

The current C runtime has eight device records and four task slots. A host regression test checks the Python limits against `firmware/include/uthome/runtime.h`. The runtime is still a host-side task-slot implementation; no JSON parser, ESP32-S3 μT-Kernel port, or device acknowledgement protocol is present on `main`.
