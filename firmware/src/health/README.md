# Health monitoring

Member 1's host modules provide event counters, rate limiting, explicit queue
overflow checks and bounded heartbeat registration/checking. Calls supply a
monotonic timestamp. They do not create a supervisor task or publish to USB,
a display or a configured queue automatically.

The portable modules require serialized access. The target `shared` wrapper
provides synchronization, queue/health mapping and watchdog operations; its
quiescent reset initializes watches for the replacement configuration. The
controller schedules bounded health snapshots. See
[the runtime contract](../runtime/README.md#verification-and-limits).

An expired watch does not authorize automatic driver-resource release. Heartbeats
represent completed work, not timer activity, and do not prove fresh sensor data.
Combined workload scheduling, stack margins and delivery still need qualification.
