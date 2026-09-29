# Invalid configuration fixtures

These files must never be loaded as active node profiles. They are fixtures for
schema, configuration-manager, and integration tests.

| File | Expected rejection |
|---|---|
| `pin-conflict.json` | GPIO 8 is assigned to both I²C SDA and the HC-SR04 trigger. |
| `unsupported-protocol.json` | `can` is outside the first prototype's supported protocol set. |

Each fixture should contain only the fault named above so a test can verify the
specific validation error.
