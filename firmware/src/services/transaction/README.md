# Transaction decision component

`txrule` owns one admitted rule and selects a bounded register read. `txwire`
decodes the provisional fixed binary envelope. `txi2c` adapts execution to the
existing generic I2C driver. State/results belong to a serialized caller; no
heap, tasks or message pointers are retained.

The [experiment guide](../../../../experiments/runtime_rules/README.md) owns
the format, examples, API usage, manual C review, workflow, evidence and limits.
These modules are linked by the opt-in USB diagnostic and ESPHome comparison,
not the normal configuration application. Production MQTT/configuration-owner
integration remains separate work.
