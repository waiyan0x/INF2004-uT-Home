# Host configuration admission

This library is independent of AI and any web framework. It validates a complete
node against the reviewed catalogue, board limits and explicit build manifest,
then returns a normalized plan without mutating input or activating hardware.

```powershell
python -m pip install -r tools/requirements.txt
python -B -m tools.config_validator configs/nodes/first-prototype.json --build-manifest configs/builds/host-reference.json
```

The CLI prints a plan and returns 0 on success, or a diagnostic and 1 on rejection.
A build manifest is mandatory to avoid silently treating a catalogue entry as
available firmware. The target-pending manifest intentionally rejects devices.
Input files reject duplicate keys, non-finite numbers and oversized documents;
node input is limited to 8 KiB. Target parsers must additionally enforce their
own nesting/allocation/transport limits.

`build_plan(document, manifest, catalogue)` raises `ConfigurationError` on
admission failure. The normalized `devices` list includes generic scheduling
fields plus each instance's complete validated configuration. The C host test
adapter exercises only the scheduling projection. Target admission/binding, queues and checked lifecycle handling now exist
independently in C; physical acceptance remains pending.

The queue contract is a consuming FIFO: admission rejects more than one enabled
consumer per queue and duplicate source references. Multiple producers remain
valid when their sample types match. Monitoring must use copied snapshots rather
than consume another service's queue. See [firmware contracts](../../firmware/CONTRACTS.md).

`resources.queue_payload_bytes` is logical depth x 64 demand. The current C
message bus reserves a fixed 2,048-byte payload pool plus queue metadata even
for smaller plans. Planner totals do not establish whole-application RAM use.

The local configurator calls this function, renders diagnostics and a review,
and sends the reviewed profile to independently validating firmware.
Do not pass the host plan directly into its old node-level AI recommendation API.
The host and target must agree on schema, board capability, descriptor and build
versions before activation.

Schema 2.1 plans include a normalized `rules` list with resolved source/output
indices. Schema 2.0 remains supported for existing sensor/display profiles.
Target manifests also enforce `board.target_limits`, including reserved native
UART pins and the two-pair capture limit. Connected UI review obtains driver
availability from firmware, separately from hardware acceptance.
