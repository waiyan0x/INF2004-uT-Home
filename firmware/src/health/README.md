# Health monitoring

Member 1's host modules provide event counters, rate limiting, explicit queue
overflow checks and bounded heartbeat registration/checking. Calls supply a
monotonic timestamp. They do not create a supervisor task or publish to USB,
a display or a configured queue automatically.

The caller explicitly registers watches, submits heartbeats and checks queues.
Runtime restart/configuration does not yet remove or replace watchdog entries.
Do not treat an expired or stale watch as an automatic instruction to release a
driver resource. A heartbeat should represent completed work, not timer activity.

Target integration must add synchronization, watch ownership/removal, queue and
runtime event mapping, stack measurements, supervisor scheduling and bounded
report delivery. The host module uses static state and requires serialized access.
See [the runtime integration record](../runtime/README.md#verification-and-limits).
