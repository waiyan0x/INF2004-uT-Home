# Generic I2C register-read primitive

`geni2c` is a portable, fixed-storage primitive for one reviewed register-pointer
read, intended for future driver adapters. A recipe contains a 7-bit address,
one-byte register, bounded read length and optional read-address pause of at most
10 ms. Open copies the recipe and claims the
address; read uses one deadline and only publishes complete successful bytes;
close releases the claim after the worker stops. Calls on a context are serialized.
The optional four-byte little-endian scaling helper uses a reviewed integer ratio,
rounds down and rejects values above the caller's validated maximum.

The caller must supply an independently reviewed recipe. This module alone does
not enforce a catalogue allowlist, identify the device, control enable pins,
decode measurements or make AI output safe. It compiled in a fresh ESP target
build but has no configuration registry binding and must not be exposed to
model-generated settings. Unused code may be removed by the linker.
Integration requires target admission of reviewed recipes, lifecycle/binding,
telemetry and board evidence for the integrated firmware.

The local build-only evidence is
`build/datasheet-work/generic-target-build-evidence.json`; it records the source
hashes and compiler profile for the earlier primitive build. No board was flashed
or tested for that build.

`senlux` is a fixed-storage SEN0390 sequence used by the provisional reviewed
adapter. It reserves address 0x4A, drives a borrowed EN output low for at least
one second, raises it, waits a further second and then reads four bytes with a
10 ms in-transaction pause. This is a provisional bench starting point, not a
manufacturer timing specification. The first transaction was NACKed in multiple
isolated captures, including one with a two-second high wait. The cause is not
established. Read results use integer millilux; zero is a valid reading. Normal
firmware does not advertise this sequence. A provisional worker and profile
binding publish typed light samples and retain the EN line until sensor cleanup.
Short host and diagnostic-board checks passed within their stated scope; full
board rollback and combined-system qualification remain open.

An isolated BMP388 chip-ID read succeeded immediately before the light sensor's
first-read NACK. That narrows the failure but does not identify its cause. The
[manufacturer library](https://github.com/DFRobot/DFRobot_B_LUX_V30B/blob/master/DFRobot_B_LUX_V30B.cpp)
repeats light reads during startup; its loop is unbounded and also treats zero as
not ready, so it is not a suitable implementation of this runtime's contract.
Investigate the NACK without masking faults, then extend the four-device board
test and assess the existing radar fault before advertising the driver.

The SEN0390 is not qualified for this primitive. DFRobot's reference library
reads four bytes at register `0x00`, uses address bytes `0x94/0x95`, converts
the little-endian value to lux with `value * 1.4 / 1000`, and delays 10 ms after
the read address. The C3 backend now has a bounded experimental pause using the
controller's END command, retaining the mutex and original deadline across the
pause. Its hardware timing, timeout recovery and shared-bus impact are not yet
verified. Verify EN, supply and electrical/timing behavior on hardware before use.
See the [manufacturer implementation](https://github.com/DFRobot/DFRobot_B_LUX_V30B/blob/master/DFRobot_B_LUX_V30B.cpp).
Do not approximate that pause by inserting a delay between two ordinary
`i2cbus_transfer` calls: they release the mutex and use STOP, so they do not
preserve the vendor transaction. If the pause is required, any adapter extension
must retain exclusive bus ownership and one absolute deadline across the whole
transaction, handle NACK/timeout and reset safely, and be reviewed against the
pinned C3 controller before target tests. Measure the effect on the BMP388 when
the devices share the bus. The light-value bytes alone do not establish mode,
freshness, correct physical units or accuracy.

Manual BARR-C review: the module uses C99 and paired public APIs; all loops are
delegated to bounded bus operations. Context and recipe are copied, initialized,
owned by the caller and used under serialized access. Buffers are fixed; output
is untouched after a failed transfer. The address claim persists across failures
until successful close, which does not write the sensor. No heap allocation,
kernel object, new task, interrupt state or compatibility exception is introduced.
Byte casts widen before unsigned shifts; uint64 multiplication holds both uint32
operands and the result is checked against the uint32 maximum before narrowing.
Input, output and context storage must not overlap. The target adapter, timing
and hardware behavior are not reviewed or qualified by this host-only primitive.

The pause and `senlux` review checks initialized fixed-size storage, a 0–10 ms
pause bound, positive residual deadline, mutex ownership across both controller
sequences, STOP on success, controller reset on failure, and restoration of the
normal hardware timeout. The sensor retains its address claim on partial setup
or enable failure until checked cleanup succeeds; waiting is nonblocking and
timestamps are captured after each GPIO transition. Integer scaling is bounded
before output. Host fakes cannot prove actual SCL timing, controller timeout
settings, peripheral recovery or coexistence; those require target tests.

The provisional worker and binder use fixed contexts, a copied bounded sensor
configuration and the existing worker and queue. The enable line is claimed low
only during activation; cleanup disables the sensor before releasing its bus
claim and GPIO. Failed cleanup retains ownership for retry. Display and enable
outputs reserve separate line slots. A host-only build with the light capability
enabled exercises a four-device profile, failed GPIO acquisition, sample and
NACK publication, and retained cleanup followed by recovery. The production
capability remains disabled. A short diagnostic application capture is recorded
in the [evidence index](../../qualification/README.md); it does not establish
long-run timing, a light-worker stack margin or full-system allocation compliance.
