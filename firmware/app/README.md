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
route configured sources to the existing PWM component. The single-broker wireless demo has scoped physical acceptance; broader workloads require separate qualification. The current rule-based
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

## Network observation and C review

`c3net_observe` serializes the existing station and MQTT snapshots for USB
command 10. Payload version 2 contains 40 explicitly encoded bytes, including
the MQTT state and failure/replacement/discard counters. The application owner
serializes observation and broker steps. The host also accepts the earlier
24-byte version 1. See the [wire contract](../src/config/README.md#wire-version-1).

The BARR-C review covers C99 declarations, matching public header, initialized
typed locals, bounded writes, fixed-width unsigned encoding and the existing
signed-error conversion. Pointer/capacity checks precede snapshot calls and
buffer writes. Failed status reads preserve the output buffer and size.
The MQTT-disabled build emits the initialized unavailable view. The additional
view uses 16 bytes of automatic storage and copies existing counters. SDK calls
remain in the transport owner. The generated C test fixture checks output
sentinels, failed reads and enabled/disabled builds through the real serializer,
with connection/read/write entry points unresolved at link time. Assertions
cannot be disabled. The pump retains its 48-byte maximum write chunks for the
64-byte response, avoiding a full USB packet requiring a terminating ZLP.

Current software checks are described in the [test guide](../../tests/README.md).
The [resource assessment](../../docs/reports/wifi-static-memory/README.md)
records the qualified release image, current RAM budgets and workload limits.
[Physical verification](../../tools/configurator/README.md#physical-verification)
covers the Desk, MQTT, BMP388, LD2450 and Home Assistant workflow.
