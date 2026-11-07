# Generic I2C register-read primitive

`geni2c` is a portable, fixed-storage register-pointer read primitive. `recinst`
(recipe instance) now binds it to the ordinary configuration application as
`i2c_recipe` descriptor version 1.0.0. A reviewed profile can change the address,
register, read length, selected bytes and polling timing without changing the
firmware. No external AI service is needed to execute an admitted profile.

The supported operation is one register-pointer byte followed by a repeated-START
read. This is not an arbitrary write/script engine. It performs no initialization
register writes, GPIO enable sequence, physical identification or unit conversion.
Review the actual device, wiring and register side effects before applying a
recipe. A datasheet or AI suggestion alone is not hardware verification. Generic
SPI/UART recipes and configurable sensor-data inference are not supplied here.

## Profile register recipes

A recipe uses an existing I2C interface and canonical lowercase address
`0x08` through `0x77`. Admission rejects an address already reserved by any other
I2C instance, including disabled instances and device-specific drivers. Replace
an existing instance when testing the same sensor through the recipe engine.
A profile change still replaces the whole plan using checked cleanup/rollback.

All behaviour fields are required: `profile: periodic_sensor`,
`operation: read_register`, `register` (0..255), `read_length` (1..16),
`read_pause_ms` (0..10), `select_offset` (0..15), `select_length` (1..16),
`interval_ms` (200..60000), `timeout_ms` (10..100) and `destination`.
The selected slice must fit inside the read, and pause must be below timeout.
At most two recipe instances are admitted, each with at least 3072 stack bytes,
within the existing combined task/stack/queue limits. No budget is enlarged.

The optional pause occurs after the read address, while the existing backend
retains bus ownership. It is not a general startup or write-to-read settling wait.
One absolute deadline includes bus-lock contention, pause and transfer. The
worker publishes only the selected successful bytes to a `raw_sample` queue;
length is explicit, unused bytes are zero, and failed reads publish fault records
with no retained value. USB telemetry uses payload version 5 when a raw queue is
present. The OLED renderer can show raw bytes in hexadecimal, although a profile
must also admit that route. Changing a register cannot make bytes a calibrated
measurement. Use device-specific drivers when compensated physical units are needed.

The reviewed BMP388 chip-ID example reads register `0x00`, expecting byte `0x50`.
A second profile can read status register `0x03` on the same firmware. This
checks configurable register access, not sensor accuracy, arbitrary-device
support, full timing acceptance or MQTT/Home Assistant integration.

Bench observation, 4 November 2026 (build `recipe-20261104-r1`, C3 MAC
`d4:05:92:e6:c3:bc`, BMP388 at `0x77` with OLED and LD2450 wired): Desk applied
`i2c-recipe-demo` (revision 925) and the monitor showed fresh byte `50`. The
recipe form then changed only the register to `0x03`; revision 926 showed `70`
without reflashing or a board restart. `c3-protocol-demo` was reapplied as
revision 927 and BMP388 readings resumed. The LD2450 reported faults throughout,
including before the reflash. A full flash backup was taken first; it is local
only. This is a short observation, not timing or combined-system acceptance.

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

## Recipe C review and verification scope

The recipe adapter and admission changes use C99, paired headers and fixed
caller-owned storage. No task, kernel object, heap allocation or compatibility
exception is added. The register engine copies admitted settings before worker
start; casts narrow only independently bounded fields. Read/selection loops and
byte arrays have fixed bounds. Failed I/O never copies old bytes to output; cache
validation rejects invalid lengths and nonzero trailing bytes. Serialized address
ownership includes disabled and cross-driver conflicts. Cleanup releases claims
only after workers stop, and existing whole-plan rollback reconstructs settings.
The 128-byte activation-settings limit is checked before activation.

Integrated host C tests exercise changing selected bytes without adding a driver,
NACK/timeout publication, raw wire bytes/padding, clock-fault clearing, rejected
plan preservation, failed activation with rollback, and returning to the BMP
adapter. Independent target-admission tests compare bounds and duplicate-address
rejection with host admission. The raw sample fits the existing 64-byte queue
item limit; the real target build and unchanged fixed-memory budget must pass
before installation. These software checks do not establish electrical safety,
physical sensor identity, measurement accuracy or complete hardware acceptance.
