# Display service

The display service keeps the latest item from each configured source queue and
builds the 128×64 status view. It renders at `refresh_interval_ms` instead of
blocking on any one sensor.

The first prototype view includes environment readings, presence, distance,
and the latest system-health state. Missing or stale samples are shown
explicitly rather than displayed as valid measurements.
