# Configuration manager

The manager converts a candidate JSON document into an immutable runtime plan.
It performs these stages in order:

1. Enforce input-size and nesting limits before parsing.
2. Parse JSON and validate it against `schemas/profile.schema.json`.
3. Check semantic rules that JSON Schema cannot fully express.
4. Resolve driver names through a fixed component registry.
5. Reserve interfaces, pins, queues, task slots, stacks, and buffers.
6. Activate the complete plan atomically or retain the active plan.

Semantic checks for the first prototype include unique IDs, valid references,
GPIO conflicts, supported drivers and protocols, I²C address range, UART and
SPI parameters, positive timeouts, bounded queue depths, task limits, and the
HC-SR04 Echo-divider declaration.

Unknown properties are rejected so misspelled safety or timing fields do not
silently use defaults. Secrets are never accepted in a node profile.

The last-known-good profile is replaced only after validation and successful
activation. A rejected update produces a structured health event containing a
stable error code and JSON path, without echoing the whole document.
