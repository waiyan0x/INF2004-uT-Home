# Application entry point

`c3app.c` composes the generic configuration controller, target admission and
binding, checked plan activation, native USB and two-slot flash storage. It owns
static contexts and runs from the existing kernel `usermain` entry point.
Device behavior stays in drivers and services; no prototype equipment identity
is embedded in this application loop.

The qualifier's default remains the runtime acceptance fixture. Build with
`--runtime-port --configuration-app` to select the configuration application
and its dedicated partition table. Startup attempts one independently validated
restore, then listens for framed requests. Active and saved revisions are
reported separately. Initialization failure halts without reinitializing live
contexts. Routine transfer faults keep listening and preserve active devices.

See [USB/storage contracts and review](../src/config/README.md#transport-and-request-identity) for build
commands, ownership, partition layout, reconnect behavior and acceptance limits.
Telemetry/health and [display services](../src/services/TELEMETRY.md#oled-rendering) are now
integrated. The [local UI](../../tools/configurator/README.md) discovers compiled
capabilities and uses this endpoint. [Typed rules](../src/automation/README.md)
route configured sources to the existing PWM component. Combined wireless/hardware acceptance remains open. The current rule-based
transaction owner is documented in [transaction services](../src/services/transaction/README.md).

Optional networking is selected with `--network-services ble|wifi|both`
and `--static-net-queues`. The `c3net` owner hooks run alongside the existing USB
pump. MQTT carries runtime rules/results and copied sensor telemetry; BLE keeps
its existing read-only diagnostics. See the [network guide](../src/network/README.md).

Full saved-profile Apply, export, restore and recovery use USB. The ESP HTTPS
server and upload arbitration were removed on 17 November 2026 at the user's
request. USB parsing still pauses while an asynchronous runtime transaction
retains peripheral pins. The same application owner serializes USB and MQTT;
no second configuration controller, task or lock is introduced.
