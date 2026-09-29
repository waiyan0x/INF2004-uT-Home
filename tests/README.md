# Verification

Run from the repository root with GCC on PATH:

```powershell
python -m pip install -r tools/requirements.txt
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
python -B -m unittest discover -s tests/target -v
```

The foundation tests check catalogue/board/build admission, alternative plans,
independent instances, rejected-candidate preservation and resource conflicts.
They compile the native runtime/component/registry suites and a test-only plan
fixture. That fixture maps validated driver IDs and scheduling into a registry
and bound runtime, then verifies independent callback counts with one binary.
It is not a firmware JSON loader or real peripheral implementation.

The runtime runner compiles and executes C suites from `tests/unit`,
`tests/integration` and `tests/fault_injection`, including Member 1's original
coverage and the new binding, transaction and target-failure guards. Native builds
use `-Wall -Wextra -Werror -pedantic`; artifacts use temporary directories.
The foundation and runtime runners intentionally overlap the three original
component/registry/runtime C suites; their totals are not unique C test counts.

Shared-sample checks exercise real message-bus copying, independent source
identity, timestamps beyond the 32-bit millisecond range, all three target slots,
error status and node-level health records. C99 constant array bounds reject
layouts exceeding the 64-byte queue ceiling at compile time. Foundation routing
tests reject competing active consumers while preserving shared producers and
disabled-consumer configurations.

The new failure tests inject failed stop, prepare, start, reconfigure and rollback
operations. They check task ownership, retained configuration/bindings, no leaked
slots and quarantine after recovery failure. Bound execution tests exercise
per-instance reads and typed queue publication, bounded catch-up and explicit
stream dispatch. The target guard links the unselected target port without the
kernel definition, and verifies that it cannot fall back to host success.

The target tests check revision/dirty-tree rejection, protected build
directories, archive containment, source snapshots, failure evidence and Windows
paths. One compiles the actual target adapter against a deliberately small fake
kernel to inject creation/start/reconfigure/delete failures, cancellation timeout
and cleanup failure; it also checks retained ownership and capacity limits.
These native tests do not simulate scheduler execution.

The same target failure executable now includes `target/periph.c`: the
portable I2C register/time fake, unchanged Bosch compensation code, BMP388
instance adapter, compiled registry and real activation/shared modules. It
checks two distinct sensors, exact analytic values, timeout/disconnect records,
address claims, bad calibration, cleanup retry and plan recovery. Its bus clock
is deterministic; it does not qualify the C3 backend's wire timing or mutex.

The separate [runtime port board test](target/runtime_port_board/README.md)
executes the real adapter with the pinned kernel. Actual build and board outcomes
are recorded under `firmware/qualification/`.

The legacy AI suite remains runnable with
`python -B -m unittest discover -s ai/tests -v`; it does not qualify the platform
or authorize training against the old contract.

Broader kernel concurrency, real HAL operations, electrical checks,
hardware activation rollback, flash recovery and timing/memory measurements
remain separate target integration work. See
[the integration record](../firmware/src/runtime/INTEGRATION.md).

The SPI/OLED fixture `target/oledtest.c` extends the target host suite with
pin collisions, wrong-owner release, distinct CS/contexts, bounds, reset delays,
frame ownership, timeout/failure retry and retained activation cleanup. Its fake
kernel invokes steps manually; it is not physical concurrency or display proof.

UART software tests extend `tests/target/uarttest.c` through the target host
suite: fragmented/malformed frames, signed-magnitude limits, independent
instances, bounded polling, timeout despite noise, RX overflow, short TX,
ownership, typed samples and activation cleanup recovery. The build-helper
suite also checks resource-patch failure recording before compilation.
Physical UART/radar acceptance remains deferred.

Checkpoints 6c/7 extend existing admission, binding, wire and service fixtures
with compiled-capability parity, typed rules, hysteresis/cooldown, stale/fault
zero output, failed command/cleanup and alternate-source routing. Nine local
UI tests cover review tokens, state/capability changes, uncertain deployment,
HTTP boundaries and schema support. Run `python -B -m unittest discover -s tests/target -v` as one line. The combined checkpoint has 151 passing host tests
across all four suites, plus an offline Edge smoke check; none constitutes
physical PWM, peripheral, USB or power-loss qualification.

The preserved Member 5 toolkit has its own Python suites and schema-1.0 fixtures.
Run `python -B -m unittest discover -s tests -v` from `legacy/member5`; this checks
the legacy host workflow, not C3 deployment. CI runs it separately.
