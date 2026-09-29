# Target plan activation

Implemented at checkpoint 2 on 2026-09-28. `activate.c` replaces one complete,
resolved plan using the real periodic-worker port. It has no driver-name
branches, parser, persistence layer or allocation framework.

The separate host `runtime.c` retains its simulated transactions. Do not call
its bound-apply API to activate the board: target snapshot transactions remain
unsupported. Link `activate.c`, `shared.c`, `tkport.c`, component, message-bus,
health and watchdog modules with `UTHOME_RUNTIME_UTKERNEL` and the pinned kernel.
The [acceptance project](../../../tests/target/runtime_port_board/README.md)
is the working build example.

## Application contract

One long-lived control task calls `activate_init` once and owns apply, recovery
and status queries. Other tasks, interrupts and callback re-entry are rejected.
The application resolves reviewed descriptors, registry bindings, settings,
wiring and routing before constructing `activate_plan_t`. This typed C plan
is an internal interface; it is not an accepted USB or JSON configuration format.

Each enabled instance supplies distinct context storage, component configure
operations, bounded step/cleanup callbacks, scheduling and a heartbeat deadline.
The manager copies up to 128 configuration bytes per instance. Store scalar
settings there, not peripheral handles or pointers to resources. Configure
callbacks copy the byte array into an aligned local structure before reading it.
They must reconstruct their resources from those settings and support cleanup
after partial configuration.

Admission checks bounded names, unique names and contexts, required callbacks,
up to eight instances, four active workers, 16 KiB of worker stacks, priorities
5..19, stacks of 1024..8192 bytes in 256-byte increments, and positive periods.
Heartbeat deadlines must be at least the period. Periods/deadlines fit a positive
signed 32-bit kernel timeout. Zero-period streaming is deferred to the UART step.
Driver-specific timeouts and heartbeat margins still require adapter admission.

Plans contain at most four queues, depth eight and 64-byte items, with
`drop_oldest` overflow. Queue handles are their zero-based plan positions.
A device's consumer bit mask declares its consuming queues; conflicting enabled
consumers and out-of-range bits are rejected. The trusted binding adapter must
map actual callback routing and sample source indices to these declarations.
The manager cannot inspect arbitrary callback behaviour or validate physical
wiring. Multiple producers may publish to the same queue.

## Replacement and recovery

Apply validates before touching the working plan, then closes the dispatch gate,
stops all workers, checks cleanup and deletion, clears shared state, configures
the replacement and prepares its dormant workers. All starts occur behind the
closed gate. Only a fully prepared and started plan is released for execution.
The gate prevents new steps; an in-flight step must still finish before cleanup.

| Result | Meaning and caller action |
|---|---|
| `0` | The replacement was accepted and released. Inspect status for later worker faults. |
| `UTHOME_ACTIVATE_REJECTED` | Admission/ownership was rejected, or activation failed and the previous plan was reconstructed. Query status and its error fields. |
| `UTHOME_ACTIVATE_RECOVERY` | Stop, cleanup, deletion or restoration failed. Preserve borrowed storage and call `activate_recover` to retry. Further apply calls are rejected. |

The status includes state, generation, retained instances, activation/recovery
errors and asynchronous worker failures. Generation advances after each
successful release, including reconstruction of the previous plan. It is a
runtime generation, not a durable configuration revision. `ACTIVE` means an
accepted plan; it does not assert that every peripheral remains healthy.

Old and candidate contexts/callback tables must remain alive throughout apply.
After successful replacement, retain the new plan's contexts; old contexts that
are no longer referenced may be released. After rejection with successful
restoration, retain the old plan's contexts; unused candidate contexts may be
released. In recovery, retain both sets until recovery succeeds. Applying an
empty plan shuts down workers; `EMPTY` with zero retained instances confirms
that this manager owns no instance resources. Do not mix raw port lifecycle
operations with manager-owned workers.

Cleanup runs in the owner task only after dormancy, must be finite and retryable,
and must leave outputs safe. Successful cleanup is not repeated when only task
deletion needs retrying. Failed restoration retains its partial resources for
the next explicit recovery attempt. Restoring settings does not restore queue
contents, counters, sensor history or past physical effects.

## Shared operations

Workers and the control/supervisor task use `shared.h` for queue, health and
watchdog access. Never mix these wrappers with concurrent calls to the unlocked
underlying modules. Each wrapper acquires the port's nonblocking atomic guard
for one short operation. Interrupt calls are rejected.

`UTHOME_PORT_ERR_BUSY` means the operation did not run: retry on a later bounded
invocation. Never spin, sleep, perform driver I/O or call lifecycle operations
while holding the guard. This avoids a high-priority task waiting indefinitely
for a lower-priority task it has preempted. Do not interpret BUSY as successful
publication or a new sample. Queue-empty is an ordinary consumer outcome.

Whole-plan replacement clears queues, health counters and watchdog registrations
only after all workers are released. Disabled/removed instances therefore leave
no stale heartbeat registration. A zero step result records bounded progress;
recoverable I/O failures can report their sample/health error and return zero.
A nonzero step result stops the worker and remains visible in status. The
application supervisor must call `shared_check`; no new supervisor task or
automatic whole-plan restart is introduced here.

## Verification and limits

The mandatory BARR-C gate passed for 70 C/header files; 113 host tests passed.
The two new modules also compile separately as C99 with warnings as errors.
Real-kernel tests on COM8 passed **514 checks on each of two USB-reset runs**:
the earlier worker tests, a start barrier, typed queue exchange, enable/disable,
replacement, asynchronous failure reporting, failed configuration/restoration,
cleanup retry, six replacement cycles and empty-plan watchdog reset.
Kernel create/start/delete faults additionally run in the host fake-kernel suite.

See the [build and board record](../../qualification/esp32c3-activation-20260928.json).
No external peripheral, JSON transfer, flash persistence, stream readiness,
power-cycle recovery or timing bound is qualified by this fixture. The target
peripheral manifest remains empty; AI work remains deferred.

Manual BARR-C review covered the rules listed in the project checklist:
bounded indices/unsigned masks, configuration alignment/lifetimes, initialized
outputs, copied settings versus borrowed resources, private/public boundaries,
owner checks, gate/guard synchronization and retained cleanup on every failure.
New modules use C99; atomic operations stay in the two already approved C11
files. Existing kernel-interface exceptions and the approval scope are unchanged.

## Configuration binding resource hook

Optional plan-level configure/release callbacks bracket all worker allocation
and cleanup. Status adds b_resources_retained; recompile callers of the
in-process plan/status ABI. activate_clear releases to EMPTY without restarting
old settings; activate_recover still restores the previous plan. See
[ownership and tests](../config/ADMISSION.md).
