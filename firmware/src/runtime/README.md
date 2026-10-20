# Generic runtime

The host runtime combines platform admission and instance bindings with
Member 1's lifecycle, resource accounting, restart and jitter APIs. It has no
device-model branches. Member 1's code is retained through the reviewed integration history.

On target, use [`activate` and `shared`](README.md#application-contract) for checked periodic
plan replacement, recovery and serialized queue/health access. The host model
described below retains its existing transaction APIs.

## Binding and execution

1. Validate the complete node using the shared planner and board/build limits.
2. Resolve each driver with `drvreg_bind`, supplying distinct, suitably
   aligned contexts and the matching instance ID.
3. Supply each component and its bounded execution callback to
   `runtime_apply_bound_devices`. Bindings are copied, contexts are borrowed.
4. Advance host time with `runtime_tick`. Periodic bound instances perform
   at most one step per tick; missed activations increment `skipped_periods`.
   A zero-period stream requires `runtime_dispatch_device` on readiness.

The step callback owns the selected read/write/publish sequence and routing;
those choices never depend on device names inside the runtime. The caller must
keep component operations, component context and step context alive until
successful unbind or whole-plan replacement. A stopped instance retains its
binding. Bind/unbind is rejected while a task resource is prepared. Aliased
component-context pointers are rejected; allocation sizes, alignment and other
shared resources remain the caller's responsibility.

`apply_devices` without bindings remains a scheduling-only compatibility model.
Its ticks increment simulated run counts and perform no driver operations.
Neither API parses JSON or activates hardware. The tests project normalized
plan fields into these APIs; production JSON admission and binding use the separate target path below.

## Lifecycle and recovery

Prepare reserves a dormant resource; start starts it; stop releases it only
after backend success. Both running and dormant resources count toward four
peripheral tasks and 16 KiB of stack; eight instances can be registered.
Reconfiguration keeps a previous configuration for explicit rollback. Changing
stack size recreates the task resource. Successful restart retains metrics and
increments `restart_count`. A failing execution step records ERROR and suppresses
further dispatch until an explicit lifecycle action.

Host transactions snapshot task resources and runtime metadata. Failure restores
the previous state, including bindings. If restoration fails, the runtime returns
`UTHOME_RUNTIME_ERR_RECOVERY`, marks entries ERROR and prohibits further mutation
or dispatch. Resource inspection also returns that error; its output must not be
used as an allocation guarantee. An explicit host `init` resets the model.
Hardware recovery requires a separate checked strategy.

Apply never calls driver operations: externally owned driver contexts, queues,
watchdog registration and hardware effects are outside its transaction. Successful
whole-plan apply resets runtime metrics. Queues and health state are separate.

## Backend selection

Link `runtime.c` with `hostport.c` for the host model. The target activation
path links `activate.c` and `shared.c` with `tkport.c`, component, message-bus,
health and watchdog modules. Define `UTHOME_RUNTIME_UTKERNEL`
and link the pinned `mtkernel` component to select real kernel calls; without
that definition the native guard continues to reject every operation. There is
no host fallback. `hostport.h` exposes host-only fault injection.

The target now implements the periodic-worker primitives documented in
[workport.h](workport.h). Prepare requires bounded step
and retryable cleanup callbacks plus an independently owned context. The task
that initializes the port owns all lifecycle/query calls and must stay alive;
ISRs, other tasks and callback re-entry are rejected for lifecycle calls.
Workers also use the monotonic clock and task-only shared-operation API;
`shared.h` provides serialized queue/health wrappers. Raw modules and drivers
are not made thread-safe merely by selecting this backend.

Prepare creates a dormant kernel task. Start can immediately preempt its caller.
Workers run periodic steps and skip missed periods instead of catching up in a
burst. Step failure exits into a dormant, still-owned task. Stop requests
cancellation and wakes a sleeping worker, waits up to 500 ms for dormancy, then
runs cleanup in the owner task and checks deletion. Timeout, cleanup failure or
delete failure retains the handle/context; retry stop without releasing storage.
Successful cleanup is not repeated if deletion needs retrying. The wait bound
requires bounded/yielding callbacks; cleanup needs its own finite bound.

Handles are not reused within a boot. Admission enforces four workers, 16 KiB
of stacks, priorities 5..19 and unique context pointers. Target reconfiguration
only accepts a never-started dormant task with unchanged stack size. Live plan
updates and stack replacement use the activation manager's stop/recreate path.
Native stream readiness is not implemented; the radar uses a bounded periodic polling worker.

The generic `runtime.c` still relies on transactions. Target transaction calls
and scheduling-only prepare deliberately fail: this worker layer does not make
`apply_bound_devices` operational on the board. The separate target
`activate_apply` reconstructs resources from copied settings instead.
Real peripheral adapters must still provide bounded configuration and retryable
cleanup before hardware recovery can be qualified.
See [the board test](../../../tests/target/runtime_port_board/README.md).

Host-model calls must be serialized. Its busy guard prevents callback re-entry; it is not
a mutex, ISR guard or task scheduler. Host ticks do not model priorities or
preemption. The low-level compatibility priority range is 1..31; platform
admission restricts application tasks to 5..19 and validates wiring, driver
requirements and routing independently.


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
signed 32-bit kernel timeout. Zero-period streaming is not supported by this activation path.
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

## Configuration binding resource hook

Optional plan-level configure/release callbacks bracket all worker allocation
and cleanup. Status adds b_resources_retained; recompile callers of the
in-process plan/status ABI. activate_clear releases to EMPTY without restarting
old settings; activate_recover still restores the previous plan. See
[ownership and tests](../config/README.md#admission).

## Verification and limits

The real worker-port fixture passed 369 checks on each of two resets; its
activation extension passed 514 on each of two resets. These are kernel/typed
plan tests, not qualification of every peripheral or flash/power-loss behavior.
See the [evidence index](../../qualification/README.md),
[board fixture](../../../tests/target/runtime_port_board/README.md) and
[software suites](../../../tests/README.md).

Manual BARR-C review covered bounded indices/unsigned masks, aligned copied
settings and borrowed lifetimes, initialized outputs, ownership checks,
gate/guard synchronization and retained cleanup after every failure. New
activation/shared modules use C99; atomic operations stay in the two approved
C11 paths. Kernel-interface exceptions and approval scope are unchanged.
The dated review and test counts remain in the qualification records.
