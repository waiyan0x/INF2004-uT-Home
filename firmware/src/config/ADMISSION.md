# Target admission and binding

Checkpoint 5b connects the configuration controller to independent JSON
admission, copied settings and the existing activation/driver interfaces. The
same firmware can switch supported sensor combinations, instance names,
wiring, queues and periods. The runtime contains no driver-name dispatch.

The original verification below records checkpoint 5b. USB/storage integration
followed at [5c](USB_STORAGE.md); telemetry and [display binding](../services/DISPLAY.md)
followed at 6a/6b. These are software checkpoints. The existing
target-pending manifest stays empty until integration and hardware acceptance.

## Small modules

| Module | Responsibility |
|---|---|
| jsonscan | Bound lexical input and nesting before vendor recursion |
| cfgjson | Exclusive bounded cJSON allocation, duplicate rejection and access |
| cfgplan | Common node/board/bus/queue admission and all physical pin claims |
| cfgdev | Reviewed descriptor versions, settings, routes and resource limits |
| cfgrule | Typed rules, bounded scalar settings and unique output ownership |
| cfgbind | Production controller callbacks and concrete instance binding |
| c3bind | Select actual C3 transports without opening hardware |

The admitted plan copies all values and retains no JSON pointers. Output is
candidate scratch, cleared on failure; never pass an active plan as scratch.
The binder owns that scratch, a candidate activation plan and eight distinct
instance slots. Allocate one noncopyable binder at application/static lifetime.
The caller must not modify or reinitialize it while resources remain retained.

Adding a driver requires a compiled adapter, reviewed descriptor admission,
tests and any new transport binding. It needs no driver-specific runtime
lifecycle edits. Profiles cannot install code or invent firmware support.

## Input and allocation bounds

- Exactly 1..8192 bytes, root object, no trailing NUL required. Reject trailing
  content, embedded NUL, duplicate decoded keys, unknown properties and
  unsupported descriptor/board/schema versions.
- At most eight nested containers, checked before cJSON recursion. Target and
  host vendor builds also set CJSON_NESTING_LIMIT=8.
- ASCII identifiers/constants, including escaped ASCII. Equivalent escaped keys
  participate in duplicate checks. NUL and non-ASCII input fail.
- **Canonical decimal integer tokens**, uint32 positives or negative int32,
  without leading zeros except zero. Reject negative zero, fractions and
  exponent spellings. Only signed rule thresholds admit negative values.
  This target encoding restriction avoids lossy conversion of integer settings.
  The host JSON-schema model accepts more spellings; the deployment client must
  emit canonical integers.
- At most **32 KiB of malloc requests including aligned allocation headers**
  and 1024 allocations per parse. Exhaustion rejects admission and frees the
  partial tree. Allocator-internal overhead and call-stack use are additional;
  this limit is not a complete RAM certificate.

Initialize cfgjson once before task startup. One control task exclusively uses
the wrapper: no other code may call cJSON, replace its global hooks or change
locale. These ownership rules do not make the allocator thread-safe. Tree
pointers expire at close; admission always closes its tree. The wrapper does
not use the vendor's global error-pointer API.

The target pins managed espressif/cjson ==1.7.19~2. The unchanged
[host mirror](../../third_party/cjson/SOURCE.json) contains the exact component
sources for offline tests, not another JSON implementation. Qualification
checks host/managed source equality and records the resolved dependency lock.
Complete MIT notices remain intact.

## Actual admission and binding support

Node schema 2.1.0 adds bounded rules; schema 2.0.0 and descriptor/board
versions 1.0.0 retain their existing fields.
Admission checks unique device/task/interface/queue identities, driver and bus
references, descriptor settings, queue sample types, single enabled consumers,
unique source queues, I2C address collisions, exclusive UART ownership, task
priorities 5..19, aligned stacks, four workers, four queues/depth eight and
16 KiB of peripheral stacks. Disabled instances reserve wiring and identity,
but no active worker/stack.

The concrete C3 backend adds these restrictions to the general host catalogue:

| Binding | Current limit |
|---|---|
| Ordinary peripheral pins | 0, 1, 3, 4, 5, 6, 7, 10 |
| UART | Port 1, RX20/TX21, 512-byte buffer |
| I2C | Port 0, 10..400 kHz; BMP388 at 0x76/0x77 |
| SPI declaration | Host 2, 100 kHz..8 MHz; no GPIO2 CS |
| Pulse capture | Two configured pairs, including disabled entries |
| Queue formats | Environment, targets, distance, health |

The production binding mask includes **BMP388, LD2450, HC-SR04, SSD1306 and PWM output**. They use
their existing registry configure/step/cleanup adapters and typed queues.
LD2450 uses the existing bounded 10 ms polling worker. Two environmental
sensors share one bus with separate contexts/addresses. Whole-plan replacement
can change the driver occupying an instance slot.

OLED profiles now bind the 128x64 renderer and existing SPI/control-line
backends. The first-prototype profile still requests unsupported GPIO2 CS and
is not deployable. PWM uses a reviewed descriptor and exactly one admitted
rule per enabled output. See [rule routing](../automation/README.md).
Health scheduling, the sample cache and USB telemetry are implemented at 6a.

## Resource lifetime and rollback

The optional generic resource component in activate_plan_t configures before
any worker is prepared. Its release callback runs only after all device cleanup
and task deletion succeed. Even failed configuration receives checked release.
Release failure retains ownership and sets b_resources_retained. A zero
resource descriptor preserves previous plan behaviour. Resource context may
not alias any instance context.

The binder opens shared I2C/SPI only for enabled users, closing them after all
workers, address/device claims and display control lines are released. Copied scalar device settings, including
identity, fit the existing 128-byte activation settings slot. Admission cannot
mutate a live union or bus. Cleanup precedes changing a slot's union member;
rollback reconstructs old settings and resources from the copied previous plan.

The production controller callbacks map activation return codes explicitly.
The added activate_clear implements RECOVER: checked cleanup to an empty state,
without restarting the previous configuration. Existing activate_recover still
restores the preceding plan. Failed clear retains resources for a retry.

Startup must initialize cJSON and the activation owner once, then initialize
one persistent binder with c3bind_operations, and initialize cfgctrl with
cfgbind_operations and that binder. Only cfgctrl should apply/restore/clear live
profiles; out-of-band activation would invalidate its active/saved metadata.

## Verification and manual C review

The admission harness compiles ordinary modules as C99 with warnings as errors.
Tests compare accepted examples with host planning and reject malformed input,
escaped duplicate keys, unknown settings, disabled wiring/address conflicts,
unsupported pins, resource limits and ambiguous numbers. Exhaustion/noise tests
are followed by valid parses to check reuse.

The existing target-port fixture now drives production cfgctrl/cfgbind through
memory storage, the real activation manager and sensor drivers with simulated
HAL backends. It verifies two sensors on one bus, replacement with distance and
radar, typed samples, no I/O during admission, duplicate suppression, storage
failure before activation, failed-device rollback, partial bus-open cleanup,
retained close failure, explicit empty recovery and boot revalidation.
It does not simulate task execution or establish physical timing.

Manual BARR-C review covers paired modules/prefixes; initialization; fixed-width
masks and unsigned shifts; exact limits before narrowing to HAL fields; copied
settings without pointers; aligned bounded allocation; complete tree release;
bounded recursion/loops; callback lifetime and serialization; status mapping;
stopped-worker resource release and retained failures. New modules use C99.
The two-file C11 scope, five GNU99 SDK-header boundaries, 49 compatibility
exceptions, formatter, policy and approval remain unchanged. Vendor spelling
is contained without adding a new exception.

See [fresh verification](../../qualification/esp32c3-admission-20260928.json).
Ten no-I/O fixture additions give **562 expected checks**, not a captured board
result. No board was flashed, reset or accessed. The earlier 514-check
activation image remains the last physical qualification.

Next is the actual USB/storage application path, then display/telemetry/local
UI and actuator behaviour. No new teammate input blocks software. Wiring,
physical acceptance and complete resource measurement remain deferred.
AI stays last.

## Measured memory and validation

The completed ESP32-C3 ELF records cfgbind_state_t = **5,944
bytes**, including its 1,356-byte candidate, 1,880-byte
activation plan and all slot contexts. Do not add those included plans again.
The activation manager separately retains two 1,880-byte plans.
Each target parser allocation has a 40-byte aligned wrapper
header already counted inside the 32 KiB request ceiling. Transfer/controller/
store contexts from 5a are another 32,928 bytes. The board fixture does not
allocate a binder; these are type sizes, not a full application's free-RAM
measurement. Stack peaks, heap allocator overhead and service buffers still
need accounting before allocating an AI reserve.

All **125 host tests pass** (54 standard, 26 foundation, 18 runtime, 27 target).
The mandatory gate reports zero findings/tool-policy errors across 158 C/H
files. The real target build and 41 owned compiler profiles pass; the six new
modules use strict C99. No hardware execution is inferred from these checks.
