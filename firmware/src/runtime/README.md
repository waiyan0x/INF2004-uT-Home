# Generic runtime

The host runtime combines baseline 2.0.0 admission and instance bindings with
Member 1's lifecycle, resource accounting, restart and jitter APIs. It has no
device-model branches. Provenance and remaining target work are recorded in
[INTEGRATION.md](INTEGRATION.md).

On target, use [`activate` and `shared`](ACTIVATION.md) for checked periodic
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
plan fields into these APIs; production configuration/driver adapters are pending.

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
Stream readiness remains pending.

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
