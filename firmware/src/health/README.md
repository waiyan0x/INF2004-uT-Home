# Health monitoring

The health task periodically publishes a bounded snapshot to `system.health`
and USB serial. The OLED displays its latest overall state.

The first prototype tracks:

- configuration acceptance, rejection, activation, and rollback;
- device initialisation, timeout, framing, and disconnection errors;
- task state, heartbeat age, restart count, stack high-water mark, and jitter;
- queue depth, high-water mark, and dropped-item count;
- free runtime-pool capacity and uptime.

Events use stable codes such as `CONFIG_INVALID`, `PIN_CONFLICT`,
`DEVICE_TIMEOUT`, `UART_FRAME_INVALID`, `QUEUE_OVERFLOW`, and
`TASK_HEARTBEAT_MISSED`. Repeated errors are counted and rate-limited so a fault
cannot flood the display or serial channel.
