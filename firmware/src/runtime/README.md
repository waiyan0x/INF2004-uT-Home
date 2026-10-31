# Generic runtime

## Workload measurement handoff

`workcost.c/.h` supplies caller-owned saturating counters. `workmeter`, the C3
clock/transfer adapters and command 9 integrate them into the USB measurement
image. Four permanent slots reserve 176 bytes of records; only bound sources
are published. Updates/snapshots use the existing guard, with generation changes
on rebinding. No new task, queue, lock or heap allocation is needed.

Measurements cover step wall time, completion lateness, serialized bus waiting,
transfer-call time, bytes/operations and device errors/timeouts. Timeouts are a
subset of errors; waiting and transfer time must not overlap or exceed step time.
These are not isolated CPU busy time or electrical wire time. Scheduler/idle
observations have a separate contract in [telemetry](../services/TELEMETRY.md).
Missing values remain unavailable rather than zero-filled. Independent calibration
and combined radio-load qualification remain open.

All cumulative additions saturate at UINT32_MAX and set a sticky flag; saturated
windows are unusable for calibration. A 32-bit microsecond sum can saturate after
about 71 minutes of accumulated elapsed work, so use short measured windows.
Invalid observations leave storage unchanged. Caller-owned input/output objects
must remain valid and non-overlapping for the call. `ai/measurements.py` checks
snapshot identity, bindings, availability and monotonic counters before producing
deltas. It does not automatically add outcomes to model inputs.

Manual BARR-C review (rules 1-8): new helper/templates use C99, fixed-width
unsigned arithmetic and checked subtraction, explicit saturation, const input,
private helper prototypes and paired headers. There are no casts, hardware
registers, callbacks, recursion, hidden state or kernel dependencies. Ownership
and guard responsibility are explicit; bounded operations cannot yield. Host
boundary tests keep assertions enabled and warnings as errors. Standalone object sizes exclude caller state, call-chain stack and
instrumentation/locking overhead; use the linked image and runtime evidence. No exception or compiler policy changed.

## Permanent target workers

The target adapter now creates its four kernel tasks during its first successful
`rt_port_init`, with separate caller-owned 8 KiB stacks and `TA_USERBUF`.
All 32 KiB remain reserved even for an empty profile. A partial startup failure
is latched until reboot; it cannot retry object creation after startup. Prepare
checks dormancy and changes priority, then binds callbacks/settings to a reserved
slot. Stop waits for dormancy and successful cleanup before releasing the binding;
it never deletes the kernel task. Handles still never repeat during a boot.

The current 16 KiB admission limit describes requested profile stack budgets,
not actual reserved RAM. That conservative host/target limit remains unchanged
pending the complete wireless/system budget. Configuration changes cannot resize
the fixed stacks. Existing telemetry is not a measured memory budget. Radio and
other infrastructure tasks need separate reservations.

Manual review: the C11 atomic profile remains restricted to its existing files.
Caller-owned `uint64_t` arrays meet the pinned kernel's eight-byte alignment and
8192-byte size contract. Kernel callback casts retain the existing dispatcher
signature. Binding fields change only after confirmed dormancy; cancellation and
result access remain atomic. Priority changes precede handle publication. Failed
stop/cleanup retains the binding; failed bootstrap never publishes an owner.
Tests check distinct stacks, all four bootstrap failure positions, retained
ownership, stale handles, capacity, rollback and zero post-bootstrap task creates
or deletes. Actual scheduling, stack headroom and whole-system object allocation
still require target acceptance. The host double's sole added external spelling,
`bufptr`, has [owner approval](../../../tools/c_standard/static-stack-approval-20261027.json).

## Existing runtime behavior

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

## Host-model lifecycle and recovery

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

Startup creates four dormant kernel tasks with permanent caller-owned stacks.
Prepare binds an unused task. Start can immediately preempt its caller.
Workers run periodic steps and skip missed periods instead of catching up in a
burst. Step failure exits into a dormant, still-owned task. Stop requests
cancellation and wakes a sleeping worker, waits up to 500 ms for dormancy, then
runs cleanup in the owner task and releases the binding. Timeout, cleanup failure
or a failed state query retains the handle/context; retry stop without releasing
borrowed storage. The kernel task and stack persist. The wait bound
requires bounded/yielding callbacks; cleanup needs its own finite bound.

`rt_port_is_owner` rejects callback re-entry. HAL close operations additionally
accept `rt_port_is_cleanup_owner`, which is true only inside the owner's cleanup
callback and false for other tasks, interrupts and shared-operation callbacks.
This permits UART, pulse-capture and PWM teardown while lifecycle/query re-entry
and new resource creation remain blocked. The earlier ownership check rejected
these legitimate closes, leaving one retained radar instance with cleanup error
`-8` during the 26 October bench test. Hardware regression results are recorded
separately from the host tests and target build.

Handles are not reused within a boot. Admission enforces four workers, 16 KiB
of aggregate requested stack budgets, priorities 5..19 and unique context pointers.
The actual fixed stack reservation is 32 KiB even with an empty profile; the
conservative requested-budget limit is not a free-RAM measurement. Target reconfiguration
only accepts a never-started dormant task with unchanged stack size. Live plan
updates and changed stack requirements use the activation manager's stop/rebind path.
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
stops all workers, checks cleanup and binding release, clears shared state, configures
the replacement and prepares its dormant workers. All starts occur behind the
closed gate. Only a fully prepared and started plan is released for execution.
The gate prevents new steps; an in-flight step must still finish before cleanup.

| Result | Meaning and caller action |
|---|---|
| `0` | The replacement was accepted and released. Inspect status for later worker faults. |
| `UTHOME_ACTIVATE_REJECTED` | Admission/ownership was rejected, or activation failed and the previous plan was reconstructed. Query status and its error fields. |
| `UTHOME_ACTIVATE_RECOVERY` | Stop, cleanup, binding release or restoration failed. Preserve borrowed storage and call `activate_recover` to retry. Further apply calls are rejected. |

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
underlying modules. Each wrapper acquires the port's atomic guard for one short
operation. Interrupt calls are rejected. On the kernel backend, contention from
a different task permits three acquisition attempts separated by at most two
one-ms delay requests. The caller owns no guard while yielding, so a preempted
lower-priority holder can finish. Scheduler latency can extend those delays.
Same-task recursion returns BUSY immediately; the serial host backend models no
preemption.

`UTHOME_PORT_ERR_BUSY` means the operation did not run after the bounded attempts:
retry on a later bounded invocation. Never spin, sleep, perform driver I/O or call lifecycle operations
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

## Allocation and object counters

`memaudit` owns no storage and makes no kernel calls. It updates caller-owned,
zero-initialized counters for startup and normal operation. Sealing is one-way;
no Apply, discovery or rollback resets evidence. Saturation is sticky. The C3
adapter `c3audit` serializes updates with saved/restored machine interrupt state
on the single-core ESP32-C3. Its counter callbacks never allocate, log,
dereference allocation pointers or take a scheduler lock. The application-entry marker precedes idle-task creation and controller setup;
its heap counters retain that startup subset separately from SDK/kernel boot.
The idle task now uses an aligned, permanent 1 KiB stack. The controller seals after infrastructure
initialization and before restoring a profile, then copies snapshots for
[version-3 telemetry](../services/TELEMETRY.md#wire-contract).

The owned build enables SDK heap hooks and renames their two callbacks only in
`heap_caps_base.c`. Eleven kernel create implementations retain their ABI under
owned native names; link wrappers count external calls before forwarding native
validation/results. Qualification checks substitution scope. Dependency files
and revisions remain unchanged. The linker fragment places the complete counter
module and `disint`/`enaint` in internal RAM for pre-kernel/cache-disabled use.

Review: fixed-width counters, checked size conversion, saturating arithmetic,
nonnull pointers, immutable phase boundary and bounded eleven-element loops;
single-core interrupt exclusion covers every shared access. No object is created
by observation. Native callback types remain exact, with ordinary owned names
and C99; no new naming/language deviation. Host tests attack invalid kinds,
null state, repeated seals, wide allocation sizes, saturation, missing/leaked
build substitutions and C-to-Python wire parity. Target builds and live counter
acceptance must be recorded separately. Successful SDK heap hooks are not an
audit of failed allocation attempts, stack usage. Separate counters observe entry attempts to the kernel allocator
(malloc, calloc, realloc, free), including failed calls. Native internal calls
within calloc/realloc are not counted twice. Direct multi-heap primitives remain
outside these hooks and require source/call-graph review.

## Verification and limits

### Stack and workload instrumentation

`stackmon` paints only newly owned stack storage before native context creation,
then scans the untouched low-address prefix. The four permanent worker stacks
are scanned only after confirmed native dormancy. Cached minima survive binding
changes. The 1 KiB idle stack is scanned by its higher-priority controller with
interrupt state saved/restored. The first pre-application native allocation is
recognized only when exactly one allocation succeeded, its size is 8448 bytes,
and `td_ref_tsk(TSK_SELF)` confirms the same stack top/size. That is the pinned
kernel's 8192-byte initial request plus 256 system bytes. Ambiguous provenance
leaves controller stack evidence unavailable. Painting occurs before the native
context writes, never on an executing or reused task stack.

Sentinel observations are empirical lower bounds on remaining untouched bytes,
not worst-case stack proofs: unvisited paths and coincidentally identical bytes
remain limitations. Fixed reservation bytes already included in the map budget
are not added again as measured usage. The observer's duration includes scanning;
board evidence must check its interference and retain the exercised workloads.

`workmeter` assembles a worker-owned step then publishes totals under the existing
shared guard. `tkport` installs one immutable microsecond clock before startup;
missing/failed clocks, lost publication, inconsistent duration or overflow make
that binding unavailable. Collection failure does not change the peripheral's
return value. The worker's native ID selects its own meter; no driver-context
cast or device-specific inference enters this path. Four fixed records replace
no existing synchronization and create no objects. `c3cost` observes I2C/SPI/UART
calls; `costview` exposes them through the independent command documented in
[the observation guide](../services/TELEMETRY.md#read-a-snapshot).

Manual review of these additions covers rules 1/5 (C99 helpers, existing C11
atomics only in tkport/main, checked signed/size/time conversions, explicit wire
words), 2/4/6 (paired headers, named callbacks, bounded scans and four-slot loops,
separate kernel/SDK clocks), 3/8 (pinned formatting, initialized storage, guarded
failure paths and no unchecked timeout extension), and 7 (single-worker partial
steps, guarded published totals, dormant-only reset, immutable native IDs and
saved/restored interrupt state). Byte inspection uses character aliasing of
known owned objects. Controller stack identity is verified against the native
debug API before access. Heap hooks still only count; the separate boot malloc
wrapper paints a fresh cache-enabled stack before its first use. No dependency,
exception, C dialect boundary or formatter change is introduced. Tests/builds
and hardware measurements are separate evidence, not implied by this review.

Use the [evidence index](../../qualification/README.md),
[board fixture](../../../tests/target/runtime_port_board/README.md) and
[software suites](../../../tests/README.md) for scoped verification. Kernel/typed
plan tests do not qualify every peripheral, wireless load or flash power loss.

Manual BARR-C review covered bounded indices/unsigned masks, aligned copied
settings and borrowed lifetimes, initialized outputs, ownership checks,
gate/guard synchronization and retained cleanup after every failure. New
activation/shared modules use C99; atomic operations stay in the two approved
C11 paths. Kernel-interface exceptions and approval scope are unchanged.
The dated review and test counts remain in the qualification records.

Cleanup-owner change, 26 October 2026: manual BARR-C review covered the paired
public prototype and fail-closed non-kernel implementation, Boolean expressions,
existing atomic owner/guard reads, task/ISR context checks and unchanged borrowed
lifetimes. No casts, allocations, new atomic types or compatibility exceptions
were introduced. Three HAL close guards accept the narrow cleanup context;
their open paths and all existing cleanup error checks remain unchanged. Host
checks exercise cleanup failure/retry, rejection of other tasks/ISR/shared
callbacks and retained lifecycle re-entry guards. The board fixture also checks
the owner/worker distinction; building it alone is not execution evidence.

Shared-guard handoff change, 26 October 2026: manual BARR-C review covered
the existing C11 atomic profile, positive kernel task IDs, fixed three-attempt
loop and two one-ms delay requests outside ownership. The holder ID is set
before the callback and cleared before releasing the flag; a preemption between
those stores can cause a bounded yield but cannot admit two callbacks. Callback
errors propagate once, failed delays leave the other task's guard intact, and
same-task recursion performs no wait. Production code introduces no pointer or
structure ABI, allocation, interrupt handling, driver I/O, dependency or
exception changes.
The host fake checks recursion, exhausted contention and delay failure. The
real-board case deliberately starts a higher-priority contender inside a short
test callback to force preemption, with no blocking or I/O in that callback.
Its generic callback cast matches the kernel's INT/void-pointer task ABI and
the task is deleted only after confirmed dormancy. The named raw fixture task
does not become a runtime-owned peripheral worker. Keep measured bench results
separate from these software and source-review conclusions.
