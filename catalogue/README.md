# Reviewed hardware catalogue

`discovery.json` adds versioned identification policies for these descriptors.
It is checked against `schemas/discovery-catalogue.schema.json` and linked back
to exact descriptor versions by `tests/discovery/`. The PC resolver matches raw
observations against it; the firmware does not interpret the policy file.
`connections/bench-i2c.json` records the first compiled, reviewed connection
preset. Changing that JSON alone cannot change firmware probe operations.
Driver availability comes from the connected build, while protocol/settings
constraints still belong to the device descriptors. See
[discovery and recommendation ownership](../docs/README.md).

The catalogue describes supported operations and constraints independently of
node instance names and deployment examples. `boards/` owns platform capacity;
`devices/` owns peripheral settings. The host planner resolves a node's explicit
`descriptor.id` and `descriptor.version` against these reviewed files.

A descriptor contains:

- `driver_id` and `protocol`: implementation and bus requirement;
- `instance_schema`: full per-instance settings and electrical declarations;
- `bus_schema`: required bus characteristics;
- `execution`: periodic, stream or queue-consumer scheduling, period/timeout
  field mapping and units;
- `routing`: produced sample type and accepted consumer types;
- `resources`: minimum stack and maximum supported instances for that driver;
- `defaults`: example values, with no requirement to use example instance IDs;
- `provenance`: source code or hardware documentation supporting the entry.

The core node schema accepts driver/descriptor identifiers, not an enum of
sensor models. The catalogue still validates exact identities and limits. To
extend support, add a reviewed descriptor and compatible implementation to the
build registry. Node requests cannot supply replacement descriptor schemas or
limits. Referenced schemas must be local to the descriptor document.

Board capabilities distinguish eight registered instances from four concurrent
peripheral tasks. Application priorities 5..19 are configurable; descriptor
priorities are defaults. GPIO and native-UART restrictions describe the selected
C3 port. They can evolve through a versioned board/backend change, independently
of the generic node format.

Each configured device owns a separate driver context. Bus sharing does not
permit shared mutable instance state. The current BMP388 entry supports two
addresses on the board's one I2C controller; the UART entry supports one owner.
Disabled instances reserve wiring but consume no active task/stack slot.

The D3 configurator reads reviewed timing overrides from
`connections/bench-i2c.json` (preset 0.2.0). Its BMP388 values are labelled bench
observations, separate from descriptor defaults and limits. Other descriptor
identities use their own defaults; unsupported identities get no recommendation.
These host-only additions do not change the preset's compiled wire code or reads.

D4 adds `bench-uart.json` (receive-only evidence, then model confirmation) and
manual `bench-oled.json`, `bench-distance.json` and `bench-pwm.json`. Connection
pins/supplies are reviewed alternatives, not inferred from signals. OLED requires
an existing compatible queue and sole use of its SPI bus. PWM is added disabled
until a separately reviewed rule is supplied. All candidates use shared node
admission; a catalogue selection never certifies physical operation.

The build manifest is a separate capability check. A catalogue entry is not
evidence that its C3 adapter is linked. The checked-in host manifest is for host
admission tests; the target-pending manifest lists no hardware-qualified adapters. Firmware must
advertise/check its actual compiled registry, not trust a host-supplied manifest.

The SEN0390 descriptor records reviewed I2C address and GPIO-enable constraints,
with conservative provisional timing defaults. It is not hardware-qualified and
does not make the unbound driver available; the connected firmware must advertise
it before a draft can be generated. Datasheet register inconsistencies and the
manufacturer's enable/read timing require the documented bench validation.

Per-item queue storage is bounded at 64 bytes in this board profile. Sample type
names describe routing compatibility, not a native C struct ABI. Driver adapters
must normalize their output and assert it fits. JSON transport must serialize
fields explicitly. Total RAM, timing and real driver rollback need board evidence.
