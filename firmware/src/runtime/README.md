# Runtime manager

The runtime turns a validated plan into μT-Kernel objects. It creates one task
per enabled peripheral, bounded queues declared by the profile, periodic timers
for sensors and display refresh, and the health task.

## Required lifecycle

- `prepare`: reserve and initialise resources without publishing data.
- `start`: release a prepared device task.
- `reconfigure`: update safe runtime parameters such as sampling intervals.
- `stop`: cancel waits, release driver state, and mark the task inactive.
- `rollback`: restore the last-known-good plan after activation failure.

Tasks must use finite waits and emit heartbeats. Queue overflow follows the
configured policy; first-prototype telemetry uses `drop_oldest` so the display
shows current data. The runtime records dropped items, execution time, timing
jitter, stack high-water marks, and restart counts for health monitoring.

No task may allocate unbounded memory or busy-wait between samples.
