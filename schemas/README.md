# Platform configuration contracts

- `profile.schema.json` (2.1.0, retaining 2.0.0 sensor/display input) describes generic node instances and connections.
- `device-descriptor.schema.json` (1.0.0) describes reviewed peripheral capabilities.

A valid node must pass the common schema, the selected descriptor's instance and
bus schemas, build availability checks, and board/resource/routing validation.
Use `tools.config_validator.build_plan`; JSON Schema alone is insufficient.

Device names are not enumerated in the node schema. Unknown models are rejected
by catalogue resolution. Adding a compatible descriptor does not require a node
schema change. Field meaning changes do require a version bump. Schema 1.0.0
consumers, including Member 5's separate branch, need explicit migration:
add board capability version and device descriptor references, preserve instance
IDs, and remove old global model/priority assumptions.

The legacy AI 0.2.0 schemas remain separate historical prototype experiments.
They are not the source of platform limits and must not generate deployments.

Schema 2.1.0 adds optional bounded `rules` and the reviewed `pwm_output`
descriptor. Source type, unique command ownership and enabled dependencies are
semantic checks in both planners. [Rule units and behavior](../firmware/src/automation/README.md)
are part of this contract; schema 2.0.0 rejects the new fields/output behavior.
