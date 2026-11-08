# Application services

The modules at this level observe runtime state: `statusmon` takes coherent
snapshots, while `telemetry` and `costview` encode the host-visible reports.
[Telemetry contracts](TELEMETRY.md) describe their fields and limitations.

- [Display](display/README.md) owns OLED rendering and worker behavior.
- [Actuators](actuator/README.md) own PWM and relay control policies.

Sample storage lives beside its serialization owner in
[`runtime/sampcache`](../runtime/README.md), not in a consumer service. Production
consumers use `shared` to borrow coherent snapshots. The target staging tool
retains its flat service include namespace; repository and host-test inputs use
the source ownership folders. Historical qualification paths and hashes remain
unchanged.
