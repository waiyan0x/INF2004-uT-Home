# HLK-LD2450 driver

The HLK-LD2450 provides presence and target tracking through UART. The first
prototype uses `256000 8N1`, a 30-byte report frame, header `AA FF 03 00`, and
footer `55 CC` as declared in the node configuration.

The driver must:

- resynchronise after noise or a partial frame;
- reject a frame with the wrong header, footer, or length;
- decode up to three targets into signed position, speed, and distance
  resolution fields;
- timestamp a valid report and publish it as `target_report`;
- report framing, receive-overflow, and timeout counters to system health.

The UART receive path must be bounded and must never wait indefinitely.

## Software implementation

`ldparse` decodes reports, `ld2450` performs bounded UART polling, and `ldinst`
binds an independent instance to the generic registry/worker and typed queue.
The reusable transport is `uartio`; the C3 backend wraps native UART1.
See [binding, source attribution, limitations and manual C review](INTEGRATION.md).
Hardware acceptance remains deferred.
