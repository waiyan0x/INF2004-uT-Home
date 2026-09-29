# Historical prototype reference

The text below records baseline 1.0.0. Its board bring-up and electrical notes
remain reference material; its four-device AI restrictions, fixed instance
mapping, priorities and training instructions are superseded by
[project baseline 2.0.0](../PROJECT_BASELINE.md). Use the current catalogue,
board capabilities and node schema for implementation. This is not the platform
contract or a current training plan.

---

# First-prototype project baseline

Baseline **1.0.0**, selected on 2026-09-26. These decisions replace requests
for members to choose the architecture or parameter ranges. Members implement
and verify this baseline, reporting concrete incompatibilities as changes.

The machine-readable policy is
[`ai/data/sources/first-prototype-baseline.json`](../ai/data/sources/first-prototype-baseline.json).
AI contract **0.2.0** binds that policy. Device ranges below are conservative
project limits, not claims about the full datasheet range. Budgets and bench
criteria are acceptance targets; none is a measured result on our board yet.

## Board, toolchain and firmware

Use the photographed ESP32-C3 SuperMini, single-core RISC-V, native USB serial,
with Wi-Fi/BLE disabled. Require 4 MB flash and confirm the actual chip/flash
before deployment. Do not assume PSRAM. Keep the selected kernel submodule at
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2`.

Use **ESP-IDF v6.1**, commit `fff9895c82d744c7237be8847347bdd1b07c6643`, with
its prescribed `riscv32-esp-elf` package `esp-15.2.0_20251204` (GCC 15.2.0).
This released version is the first qualification candidate, chosen to match
the port's 6.1-era source and avoid following an unpinned development branch.
Source selection does not prove compatibility: build the untouched task
example, then run it on the board, before integrating the application.
[Release](https://github.com/espressif/esp-idf/releases/tag/v6.1),
[tool manifest](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/tools/tools.json).

Use an activated ESP-IDF PowerShell terminal and source/SDK paths without
spaces. [SETUP_ESP32C3.md](../firmware/SETUP_ESP32C3.md) contains the commands.
Keep module branches intact until their changes are integrated and reviewed.
The Member 2 integration tip is `feat/ssd1306-driver`, which includes the HAL
and BMP388 work; Member 3's independent tip is `feat/uart-gpio`.

## Runtime, ownership and memory

Keep four peripheral tasks and four telemetry/health queues. Queue depth is
1..8, default 8, maximum item size 64 bytes, drop oldest on overflow. Device
IDs are at most 31 characters to fit the existing 32-byte array including NUL.
The four-device limit excludes fixed service tasks such as USB and AI.

Priorities are fixed policy for this prototype; lower numbers run first:

| Task | Priority | Initial stack budget |
|---|---:|---:|
| Health supervisor | 4 | 2 KiB |
| LD2450 reader | 8 | 4 KiB |
| HC-SR04 reader | 10 | 3 KiB |
| BMP388 reader | 12 | 3 KiB |
| OLED renderer | 16 | 3 KiB |
| USB/configuration service | 20 | 6 KiB |
| AI inference worker | 24 | 4 KiB |
| Kernel example's idle task | 32 | 1 KiB |

Reserve other priority values for later reviewed needs. The AI response retains
`task_priority` for compatibility with the pipeline, but the wrapper fills it
from policy and rejects a mismatch. Do not train a model to promote itself or
a display above acquisition tasks.

The application target is a **64 KiB kernel object/stack heap**, replacing the
upstream 32 KiB default through an application-owned startup override when the
integrated build is added. This is a target allocation, not an edit already
made to the submodule. Budget internal port tasks, stacks, object overhead and
temporary reconfiguration resources as well as the table above. Queue payload
storage alone is at most 4 x 8 x 64 = 2048 bytes, excluding queue metadata.

Use one owning task per peripheral, finite waits, and cooperative stop before
reconfiguration. Recreate tasks for stack changes. Reserve a complete candidate
plan before changing hardware; on failure, restore the previous plan. Check
actual allocation headroom and stack use before accepting deployment.

The health task runs every 100 ms and reports every 5 s. A periodic task's
heartbeat deadline is its configured period plus its bounded operation time
plus 100 ms; radar must make progress or report an error every 500 ms. A missing
Echo or radar timeout is a device fault, not automatically a dead task. The
port interrupt watchdog detects CPU wedges; do not claim that it provides
per-task liveness or that ESP-IDF's disabled Task-WDT is active.

## Device decisions and training limits

| Device | Selected mode | Period: allowed / balanced default | Timeout: allowed / default | AI work-unit payload |
|---|---|---|---|---:|
| BMP388 | I2C 400 kHz, address 0x76; normal mode, 25 Hz internal ODR, pressure x4, temperature x2, IIR 3 | 200..60000 ms / 2000 ms | 10..100 ms per I2C transaction / 20 ms | 6 register bytes |
| SSD1306 | 128x64 monochrome, 4-wire SPI, mode 0, 8 MHz | 100..1000 ms / 250 ms | 5..20 ms per SPI transfer / 10 ms | 1024 framebuffer bytes |
| HLK-LD2450 | UART1, 256000 baud, 8N1, no flow control; fixed 10 reports/s | Not configurable; AI period is 0 | 200..500 ms per complete report / 250 ms | 30 wire bytes |
| HC-SR04 | 10 us Trigger, timestamped Echo edges; 20..4000 mm accepted | 100..2000 ms / 1000 ms | 25..30 ms total acquisition / 30 ms | 4-byte pulse duration |

These work-unit sizes describe each acquisition/transfer. They are not C struct
sizes, JSON lengths or queue-item sizes. Queue adapters must use their actual
fixed-width sample representation and assert it fits the 64-byte limit. Keep
raw structs out of USB messages. Convert timestamps explicitly between the
member drivers' milliseconds/microseconds and a common monotonic clock.

BMP388 settings preserve Member 2's implementation. It measures pressure and
temperature, not humidity. Host polling slower than 25 Hz consumes the latest
reading; it does not reduce the sensor's fixed internal ODR. Therefore the
`power_saving` policy initially reduces host work, not a proven sensor-power
reduction. Data-ready/freshness checks belong in the target integration.
[Bosch datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp388-ds001.pdf).

The selected OLED is a hardware requirement to implement against. The actual
module is still unidentified: verify its markings and SPI wiring before
connecting it. An SH1106 or I2C-only module needs an explicit driver/profile
change. The current SSD1306 driver cannot detect an unplugged SPI display.
[Controller information](https://www.solomon-systech.com/product/ssd1306).

The radar's fixed report rate and framing come from Hi-Link's specification.
Keep its parser unchanged and mark readings stale after 500 ms without a valid
report. Do not vary the sensor report rate by adding a sleep to the receiver.
[Hi-Link manual](https://h.hlktech.com/download/HLK-LD2450-24G/1/HLK%20LD2450%201T2R%E8%BF%90%E5%8A%A8%E7%9B%AE%E6%A0%87%E6%A3%80%E6%B5%8B%E8%BF%BD%E8%B8%AA%E6%A8%A1%E7%BB%84%E8%AF%B4%E6%98%8E%E4%B9%A6%20V1.02%20.pdf).

The sonar driver already enforces 61 ms between trigger starts. Our 100 ms
minimum leaves scheduling margin. Its 4000 mm limit corresponds to roughly
23.2 ms Echo duration under its conversion; a 30 ms total deadline covers the
full intended range. A shorter deadline must not silently truncate that range.
Use edge timestamps/capture, not a task that busy-waits for the entire Echo.

Initial operating-mode periods are deterministic training-label policy:

| Device | Power saving | Balanced | Responsive |
|---|---:|---:|---:|
| BMP388 | 5000 ms | 2000 ms | 200 ms |
| OLED | 1000 ms | 250 ms | 100 ms |
| HC-SR04 | 2000 ms | 1000 ms | 100 ms |
| LD2450 | Fixed hardware stream | Fixed hardware stream | Fixed hardware stream |

An explicit valid `desired_period_ms` overrides this table. The deterministic
wrapper copies it to the output; do not train a tree to approximate arbitrary
user-selected periods. Validate that the response preserves it. Keep the timeout
defaults above initially; timeout tuning requires evidence rather than assuming
that a power mode changes bus reliability. Active-device count and queue depth
are validated context features; this initial policy does not invent a measured
load-dependent adjustment. Dataset records must label these outputs as policy
labels, separate from later measured outcomes.

## Pin map and HAL decisions

Adopt the allocation in [configs/README.md](../configs/README.md): I2C 0/1,
SPI MOSI/SCLK 6/7, OLED CS/DC/reset 2/3/10, radar RX/TX 20/21, sonar 4/5.
Keep 18/19 for native USB and leave 8/9 unused by project peripherals.

Add a **10 kOhm pull-up from GPIO2/OLED CS to 3.3 V**. The display must not
pull CS down through reset. Confirm normal boot and download mode after wiring.
GPIO2 is a strapping pin; Espressif recommends pulling it up to avoid glitches.
[Espressif hardware guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html).

For the 5 V HC-SR04 Echo, use 10 kOhm from Echo to GPIO5 and 15 kOhm from GPIO5
to ground (nominally 3.0 V from a 5.0 V Echo). Share ground and verify the actual
divider output. Use the sensors' specified supplies and 3.3 V MCU-side logic.

**UART1 remains owned by the native port's `DEV_SER` driver.** Member 3's C3
HAL wraps it; it must not install another UART1 driver. One task handles both
RX and any future commands. Configure baud, 8N1 and finite timeouts explicitly;
upstream defaults are 115200 baud and infinite waits.

The upstream ring has 50 storage slots and reserves one empty slot. The
application integration patch raises storage to 513 for **512 usable bytes**
and disables unused native ADC startup, which would otherwise compete with
our GPIO allocation. The same macro sizes TX too. The patch is separate from
the clean upstream submodule and must be qualified after the untouched example:
[patch instructions](../firmware/third_party/patches/README.md).

The radar buffer remains fixed at 512 in every mode. At 300 report bytes/s this
is nominally about 1.7 s of backlog, but full-rate UART noise at 256000/10 bytes/s
can fill it in about 20 ms. Service it promptly; on overflow report a fault,
discard stale partial framing and resynchronise. Do not claim the buffer
protects against arbitrary noise or slow consumers.

Wrap `gpio_config`/`gpio_write` for OLED callbacks and explicitly translate
`ug_status` to `hal_status_t`. Supply real reset delays for OLED and BMP388.
Use short bounded microsecond delays only where needed; longer waits yield
through the kernel. Convert microseconds to millisecond deadlines deliberately:
AI I2C/SPI timeout outputs must be divisible by 1000. For UART, retain the total
deadline across all partial reads and do not restart the timeout per byte.

Use ESP-IDF HAL/LL for board access and the selected μT-Kernel for waits/IRQs.
Any high-level IDF driver use needs validation against this port's compatibility
layer; no second scheduler or fake-success hardware implementation is accepted.

## AI execution and budgets

Start with a shallow decision tree (maximum depth 6, maximum 127 nodes), trained
on the PC and exported as portable C. Compare it against the deterministic
policy above. A tiny integer neural network is a later candidate only if it
earns its additional runtime cost. No FPU, PSRAM or S3 acceleration is assumed.

Allocate at most **32 KiB flash** for model code/constants/metadata and **16 KiB
total RAM** for the AI module, including a 4 KiB stack and preprocessing/work
buffers. Target **10 ms worst observed wall-clock latency** under concurrent
four-device operation. These caps require measurement; they are not a guarantee
that any chosen library/model fits. No inference-time dynamic allocation.

Run inference on demand at priority 24, at most once per second, with one
request in flight. Sensors continue independently. On busy, unsupported input
or failed validation, abstain/report an error without changing configuration.
The local app performs dataset preparation, training, model evaluation, template
generation and presentation. Model updates initially travel in a rebuilt
firmware image; a live model-update protocol is outside prototype 1.

Use 800/1000 as an initial confidence threshold. It is a policy threshold, not
a calibrated probability or evidence of 80% correctness. Reject out-of-range
outputs rather than silently clamping. Target at least 99% exact agreement with
policy labels for discrete predictions per device/mode, zero accepted invalid
outputs, and exact host/C parity for tree predictions. Evaluate grouped held-out
scenarios; reproducing generated labels does not establish real-world optimality.

## Local application, mapping and USB protocol

Build on Member 5's existing local configurator; do not create a parallel UI.
Keep node schema version `1.0.0` during this unreleased prototype, updating its
board constant and bounds when that branch is integrated. AI contract version
is separately **0.2.0**. Reject old AI requests rather than reinterpret values.

The generator receives an explicit adapter output, not an AI response passed
directly into its current node-level recommendation API. Match `device_type`
through the fixed baseline `device_id`, validate the exchange, then patch only:

| AI value | Node profile field |
|---|---|
| BMP388 / HC-SR04 `period_ms` | Matched device `behaviour.interval_ms` |
| OLED `period_ms` | `behaviour.refresh_interval_ms` |
| Radar `period_ms = 0` | No interval field; hardware stream remains fixed |
| BMP388 / OLED / radar `timeout_us` | `behaviour.timeout_ms`, with checked exact division by 1000 |
| Sonar `timeout_us` | `behaviour.timeout_us` |
| Radar `buffer_size_bytes = 512` | Referenced UART `rx_buffer_bytes` |
| Fixed `task_priority` | Matched device `task.priority` |

The USB adapter must likewise reject radar timeouts not divisible by 1000;
the AI exchange validator enforces this for every millisecond-backed driver.
Pins, protocol constants, dimensions, identities and electrical declarations
remain deterministic. Preserve unrelated device settings. An abstention creates
no profile change. Validate the complete candidate, show it for user review,
and activate only after an explicit apply action and firmware validation.

Transport is UTF-8 newline-delimited JSON, one object per line, with envelope
fields `protocol_version`, `type`, `request_id`, `payload`. Protocol version is
`1.0.0`. Support `hello`, `ready`, `ai.request`, `ai.response`, `config.get`,
`config.current`, `config.validate`,
`config.validation`, `config.apply`, `config.applied`, `health`, `log`, `error`.
Responses echo the request ID; for AI it also matches the payload request ID.
An error payload is `{"code":"...","message":"..."}`. Host request IDs use
the AI contract's 1..64-character ASCII identifier pattern. Unsolicited
`health`/`log` events use request ID `event` and are never treated as replies.

Limit an encoded line including LF to 16 KiB, compact profile payload to 8 KiB,
and compact AI payload to 2 KiB. Use static bounded receive/transmit buffers
outside the AI budget, one outstanding request and a 2 s host response deadline.
Discard an oversized line through its newline without allocating more memory.
At startup, the host discards bounded boot-text lines until a `hello`/`ready`
exchange; after readiness one writer owns USB and logs use JSON envelopes.

`config.get` has an empty object payload; `config.current` returns the complete
active profile plus its revision, baseline version and AI contract version.
The initial `ready` response likewise advertises these versions. The app must
reject an unsupported version before generating or applying recommendations.

`config.validate` never activates. `config.apply` carries the complete candidate
profile; firmware validates and stages it, stops affected tasks, activates and
acknowledges only on success. Cache the last apply request ID/result for duplicate
retry within the connection; reuse of that ID with different content is an
error. On reconnect, use `config.get` before retrying an uncertain
apply. Persist the successful last-known-good profile; retain the previous
version on failed activation. Atomic storage/recovery is part of the forthcoming
configuration implementation, not supplied by the AI validator.

## Verification replaces open-ended input requests

| Owner | Next evidence to produce |
|---|---|
| Member 1 | Pinned SDK build/boot, lifecycle cleanup/restart, allocated memory/stack measurements, loaded inference timing |
| Member 2 | Actual panel matches SSD1306 SPI 128x64; BMP388 identity/readings; GPIO2 reset/boot check; bounded bus failures |
| Member 3 | 256000-baud capture and native-ring overflow recovery; single UART owner; timed Echo edges and divider voltage |
| Member 4 | Reproducible policy-labelled dataset, grouped evaluation, C export/parity and budget results |
| Member 5 | C3 schema and adapter alignment; bounded USB framing; review/apply/rollback and reconnect behaviour |

Begin with 30 minutes of four-device operation while requesting inference at
1 Hz. Require no unexplained reset, no healthy-path queue/radar overflow, zero
out-of-range accepted recommendations and the stated AI budget. Inject sensor
disconnect, malformed UART data and failed configuration changes; record finite
failure times and successful recovery. For sonar, start with 100/500/1000 mm
targets and a project tolerance of max(20 mm, 5% of reference distance).

No teammate response is needed to begin implementing or generating baseline
training data. Actual hardware identity, electrical checks and measurements
cannot be settled by a software decision. If they contradict the baseline,
record the evidence, revise the policy version and regenerate affected data.

## Configuration parser choice

Use the managed [espressif/cjson component, version 1.7.19~2](https://components.espressif.com/components/espressif/cjson/versions/1.7.19~2)
(MIT licence). IDF 6 removed its built-in JSON component. Add an exact
`espressif/cjson: "1.7.19~2"` dependency to the eventual configuration
component's `idf_component.yml`; retain the resolved lock as build evidence.
Before parsing, cap the compact profile at 8 KiB and JSON nesting at 16; reject
duplicate/unknown keys, excess devices/queues and invalid references. A parser
does not supply schema or resource validation. Bound parser allocations in a
configuration-task-owned arena/pool (initial target 64 KiB) and release it after
constructing the fixed runtime plan; reject on exhaustion. Configure any global
cJSON allocator hooks once, and serialize use. Account for the pool, USB buffers,
kernel heap and remaining application memory in the link map and measured free
memory. The AI stack counts toward both the AI module's 16 KiB limit and the
owning kernel heap's capacity; count its physical storage only once. The JSON
pool and USB buffers are outside the AI budget. Do not allocate large JSON
documents on task stacks.
