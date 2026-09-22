# Application entry point

`main.c` performs orchestration only. Device logic belongs in drivers,
services, and runtime modules.

## Cold-start sequence

1. Initialise the minimal board and USB serial path needed for diagnostics.
2. Initialise μT-Kernel and the runtime's fixed resource pools.
3. Load the candidate JSON profile from USB serial or configuration storage.
4. Parse it and run schema plus semantic validation.
5. Resolve every interface, GPIO, queue, driver, and task resource without
   starting device work.
6. If all resources are valid, commit the profile as last known good.
7. Create queues and one task for each enabled device.
8. Start health monitoring, then release device tasks to run.

If no valid profile is available, remain in a serial recovery mode with all
peripherals inactive.

## Live update sequence

A replacement profile is staged separately. Validation failure leaves the
active profile untouched. For a valid update, the runtime stops only affected
tasks, applies interface and queue changes, then restarts them. If activation
fails, it restores the last-known-good profile and reports the rollback.

The first prototype must support changing `interval_ms` and `enabled` through
this path without reflashing firmware.
