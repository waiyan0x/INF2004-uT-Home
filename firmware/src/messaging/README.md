# Messaging

The first prototype uses an in-process message bus backed by bounded μT-Kernel
queues. Queue IDs, item types, depth, and overflow policy come from the
validated profile.

Publishing copies a fixed-size typed item and never waits indefinitely.
Consumers can wait with a timeout or request the latest available item. Type
mismatches, dropped items, queue high-water marks, and timeouts are reported to
health monitoring.

`transports/loopback/` supports host integration tests. MQTT and remote-node
delivery are deferred until the local four-device path is stable; application
code must not call a transport directly.
