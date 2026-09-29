# Services

Services use the common sample types and configured source IDs. They do not own
buses or depend on a particular equipment arrangement. Adding a supported driver
does not change the runtime or require a new telemetry command.

Checkpoint 6a supplies:

- `sampcache`: bounded latest samples per registered source, alongside queues.
- `statusmon`: owner-loop heartbeat checking and configured health reporting.
- `telemetry`: explicit USB snapshot encoding, with matching Python decoding.

Checkpoint 6b adds `oledview`, rendering configured typed sources into the
existing display framebuffer. See [telemetry contracts](TELEMETRY.md) and
[display ownership, review and limits](DISPLAY.md).
Checkpoints 6c/7 add the [local browser application](../../../tools/configurator/README.md)
and [rule/output routing](../automation/README.md). Both reuse copied source
observations. Physical acceptance is next at 8, and AI is last at 9.
