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

Optional onboard monitoring networking is selected with `--network-services ble|wifi|both`
and `--static-net-queues`. The `c3net` owner hooks run alongside the existing USB
pump; configuration mutation stays disabled in the network layer. See the
[network service guide](../src/network/README.md) for exact limits and security
gaps. These services do not qualify SDK task/timer/allocation compatibility.

An optional private MQTT setup enables the bounded `netbroker`/`c3mqtt` publisher.
It shares copied sensor telemetry with HTTP/BLE, including light type 5, and
remains independent of HTTPS listener startup. SDK compatibility/resource and
physical acceptance remain open; see the network guide for the implemented
MQTT wire contract and build-only verification limits.

## HTTP configuration owner

The optional Wi-Fi service initializes cfgmux against the existing cfgctrl/cfgpump.
An unavailable mux leaves networking read-only. USB parsing pauses only during an
admitted bounded upload; sensor/display workers retain their existing lifecycle.
HTTPS Apply runs on the existing owner and refreshes metadata before later network
captures. No shared runtime/kernel compatibility source is changed. See the
[network guide](../src/network/README.md#authenticated-configuration-apply-and-usb-arbitration).
