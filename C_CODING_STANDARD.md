# Project C standard

BARR-C:2018 is mandatory for project-owned C. The source of authority is the
user-supplied [BARR-C:2018 PDF](barr_c_coding_standard_2018.pdf). This document
records project decisions and review responsibilities; it does not reproduce or
replace the standard.

## Scope and compiler profile

The policy covers owned `.c` and `.h` files, including tests and future HAL,
driver and application implementations. Vendor sources under `third_party`
and generated build output are outside the owned-code audit. Their interfaces
must be contained by adapters. Dependencies remain pinned and unmodified.

Ordinary application code should remain within C99. The existing target worker
and its board test use C11 atomics to synchronize cancellation, status and task
ownership. Foundation/runtime host suites compile as C99; the target adapter and
its host failure test compile as C11. Keep the atomic operations: replacing them
with `volatile` would not preserve the synchronization contract. This is an
**approved, scoped deviation from rule 1.1.a**, limited to the existing atomic
profile in `firmware/src/runtime/tkport.c` and
`tests/target/runtime_port_board/main/main.c`.

The user explicitly approved the retained compatibility exceptions on
2026-09-28. The sign-off is recorded under **Neoweikang**, the effective repository
Git `user.name`; the user did not separately state their name in the reply. See
the [approval record](tools/c_standard/approval-20260928.json) and
[audit sign-off](tools/c_standard/AUDIT.md#compatibility-sign-off-and-limits).
Additional C11 features or expanded scope require separate review and approval.
A green automated check neither grants approval nor certifies full conformance.

Target qualification also checks the generated compiler commands. ESP-IDF can
append its own language/warning flags after target settings, so owned source
options enforce C99 or the approved C11 profile last. Enabled warnings remain
errors, including classes the SDK otherwise relaxes. The build helper rejects
an effective language mismatch or remaining warning-error relaxation.
The C3 `c3i2c.c`, `c3spi.c`, `c3pin.c`, `c3pulse.c` and `c3pwm.c` adapters
use the GNU99 compiler dialect
because the pinned external SDK LL headers contain GNU `typeof`. Their owned
source remains C99;
this does not authorize GNU constructs in project code. Vendor headers remain
outside the owned-code audit. The exact path/profile is recorded by qualification;
the two-file C11 approval and 49 kernel exceptions are unchanged.

The kernel's fixed entry points, callback signature, SDK types and structure
fields retain their external spellings. The minimal host kernel double mirrors
only the fields used by the adapter, not the full target structure layout. The
sign-off covers the 49 existing kernel-interface exceptions identified by exact
IDs and a payload hash in the approval record, preserving their pinned interface
authority. New or altered exceptions require separate review and approval; these
exceptions must not be reused for new application interfaces. The board check
macro uses stringification, which a function cannot provide; its argument is
evaluated once and its body delegates to an ordinary function. This permitted
use under rule 6.3.a is outside the deviation approval.

## Required checks

Install the pinned development tools:

```console
python -m pip install -r tools/c_standard/requirements.txt
python -m pip install -r tools/requirements.txt
python tools/c_standard/check.py
python -B -m unittest discover -s tests/c_standard -v
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
python -B -m unittest discover -s tests/target -v
```

Use `clang-format` **20.1.8** and the repository `.clang-format`. To select an
isolated executable, set `BARR_CLANG_FORMAT` or pass `--format PATH` to the checker.
The checker does not modify files. Formatting is an explicit development action:
`python tools/c_standard/format.py --root . path/to/module.c path/to/module.h`.
Omit the paths to format all owned C/H files. The wrapper applies the pinned
formatter and a narrowly scoped pass that aligns switch-owned `break` lines
with their case labels. Nested loop breaks retain their indentation; nested
switches use their own labels. Rerun the gate afterwards.
Keep the end-of-file comment and the final blank line required by rule 3.3.c.

The standard check runs before the host C suites and target qualification builds.
The target CMake build also depends on that check. Missing tools, a different
formatter version, violations and stale exceptions fail the gate. The GitHub
workflow runs the checks and regression suites on pushes and pull requests.
Repository administrators should make that workflow a required branch check;
adding a workflow locally does not configure GitHub branch protection.

`--skip-format` is only for diagnosing lexical findings. It is not a compliant
build command and is never used by the build or CI gates. Do not add blanket
exclusions or suppress failures. A checker exception must name an exact path,
rule, symbol or source fragment, rationale and authority. A changed or unused
exception must be reviewed rather than silently carried forward.

## Module and API conventions

Use lowercase module names unique in their first eight characters. Every C
source includes its matching header. Public declarations belong in headers;
private helpers and state are `static`. Put private prototypes before public
definitions and private implementations after them. Use the templates in
`tools/c_standard/templates` when adding a module.

The runtime port is one logical module with two mutually exclusive backends.
`rt_port.h` and `workport.h` declare the common contract; `hostport.c` and
`tkport.c` implement it. Their paired headers describe the backend selection.
Link exactly one backend. This layout does not license unrelated public names.

| Area | Public prefix / header |
|---|---|
| Component operations | `component_`, `uthome/component.h` |
| Compiled driver registry | `drvreg_`, `uthome/drvreg.h` |
| Runtime lifecycle | `runtime_`, `uthome/runtime.h` |
| Target plan activation | `activate_`, `activate.h` |
| Serialized target queue/health access | `shared_`, `shared.h` |
| Runtime backend contract | `rt_port_`, `rt_port.h`, `workport.h` |
| Host backend controls | `hostport_`, `hostport.h` |
| Message queues | `msgbus_`, `uthome/msgbus.h` |
| Health counters | `health_`, `uthome/health.h` |
| Queue health observation | `hlthbus_`, `uthome/hlthbus.h` |
| Heartbeat watchdog | `watchdog_`, `uthome/watchdog.h` |
| Portable I2C bus | `i2cbus_`, `i2cbus.h` |
| C3 I2C backend / host register fake | `c3i2c_` / `i2cfake_`, matching headers |
| BMP388 driver / instance adapter | `bmp388_` / `bmpinst_`, matching headers |
| Portable SPI bus / host fake | `spibus_` / `spifake_`, matching headers |
| Pin ownership / digital output contract | `pinpool_` / `digiout_`, matching headers |
| C3 SPI / pin and output adapters | `c3spi_` / `c3pin_`, matching headers |
| SSD1306 driver / instance adapter | `ssd1306_` / `oledinst_`, matching headers |

Public `uthome_` function/type prefixes were shortened in this audit to satisfy
the 31-character identifier limit. Uppercase `UTHOME_` constants remain stable.
Instance text fields are `name`; Boolean members use `b_`; pointer members use
`p_`. Driver operation members are `p_configure`, `p_read`, `p_write`, `p_publish`.
These are source API changes: teammate branches must update callers when they
merge. Configuration JSON continues to use `id` and `enabled` unchanged.

Use combined prefixes in the standard's order: `g_`, `p_`, `pp_`, `b_`, `h_`,
or combinations such as `gb_`, `gh_`, `pb_`, `ph_`. Arrays are not pointer
variables. Use fixed-width integers when width matters, `size_t` for object
sizes, `bool` for truth values, and `int` for width-independent status codes.

Declare callback function signatures through named typedefs, then use explicit
pointers to those types. For example, `typedef int module_step_t (void);` and
`module_step_t * p_step;` preserve the function-pointer ABI and permit the
standard's declaration-pointer spacing without adding spaces inside grouping
parentheses. The checker rejects new inline parenthesized pointer declarators.
External callback casts remain subject to individual ABI and range review.

Approved vocabulary: ABI = application binary interface; API = application
programming interface; drvreg = driver registry; hlthbus = health/message-bus
integration; ID = identifier; ISR = interrupt service routine; ms = milliseconds;
msgbus = message bus; ops = operations; port = platform adaptation; rt = runtime;
SDK = software development kit; tk = external T-Kernel interface. Test filename
prefixes identify their subject (cmp, reg, rt, stats, res, integ, trans, fault).
I2C = Inter-Integrated Circuit; C3 identifies ESP32-C3; BMP388 is Bosch's part
identifier; bmpinst identifies that driver's component instance adapter.
SPI = Serial Peripheral Interface; CS = chip select; D/C = data/command line;
OLED = organic light-emitting diode; oledinst identifies its instance adapter;
MOSI/MISO identify controller-out/controller-in SPI data; FIFO = first in, first
out; pinpool is pin ownership storage; digiout is the digital-output contract.
New abbreviations must be added with their meaning during review.

## Mandatory manual review

The gate is a lexical and formatting checker, not a complete C semantic analyzer.
Compiler warnings and passing tests also cannot prove all BARR-C rules. A reviewer
must check the following for each changed module and record the result in the PR.

| Rules | Review responsibility beyond the automated checks |
|---|---|
| 1.1, 1.5–1.8 | Language/ABI profile; meaningful names; justified casts and ranges; const/static; concurrency and hardware qualifiers. |
| 1.3–1.4, 3 | Braces and expression grouping; spaces and alignment; switch indentation; one statement per line. |
| 2 | Accurate complete comments, assumptions and useful interface contracts; no dead or misleading commented code. |
| 4 | Cohesive modules, minimal includes, public/private boundaries and correct paired headers. |
| 5 | Fixed-width decisions, signedness, promotions, unsigned bit operations/literals, Boolean conditions and external data layout. |
| 6 | Descriptive names, public prefixes, named prototypes, bounded functions (make a reasonable effort to stay within 100 lines), and task/ISR contracts. |
| 7 | Initialization on every path; pointer, Boolean and handle semantics, including typedefs and macro-generated declarations. |
| 8 | One declaration per variable; shallow branching; loop termination and empty-body intent; constant-left equality; deterministic error handling. |

Review casts individually: establish valid range, alignment, lifetime, aliasing
and callback ABI as applicable. Nearby comments explain casts that survive this
audit. Compound literals are object initialization, not type conversions. Use
`(void)` only with an explanation when intentionally discarding a result or an
unused callback parameter.

Use explicit relational comparisons for non-Boolean conditions. Parenthesize
compound operands of `&&` and `||`. Put constants on the left of equality tests.
Keep `goto`, `continue` and multiple exits to a minimum as the standard prefers;
early error returns are retained where they keep resource ownership clearer.
Do not replace checked cleanup/retry paths solely to force a single return.

Host tests deliberately execute operations inside assertions. Their sources
reject `NDEBUG`, so disabling assertions cannot silently turn off those tests.
Board acceptance checks remain active in target builds and are part of the test
protocol, not optional debug logging.

Future work must satisfy both the gate and this review. Do not describe a green
gate as certification of complete BARR-C conformance or approval of deviations.

## UART module vocabulary

Public prefixes `uartio_`, `c3uart_`, `uartfake_`, `ldparse_`, `ld2450_` and
`ldinst_` belong to matching headers. UART = universal asynchronous
receiver/transmitter; RX/TX = receive/transmit; LD2450 identifies Hi-Link's
part; ldparse and ldinst are its parser and registry instance adapter.
These modules use C99 and add no compatibility or language deviation.

## GPIO module vocabulary and SDK header boundary

`digiin_` is the borrowed digital-input callback; `pulseio_` / `c3pulse_` are
portable pulse capture / C3 adaptation; `pwmio_` / `c3pwm_` are pulse-width
modulation / C3 adaptation; `gpiofake_` provides host faults. `hc_sr04_` names
the distance driver, `hcinst_` its registry instance, and `pwminst_` the generic
PWM component. Each prefix belongs to its matching header. RMT is Espressif's
remote-control pulse peripheral; LEDC is its LED PWM controller; MMIO means
memory-mapped I/O. These modules use C99 syntax. The two new C3 translation
units need GNU99 solely for pinned external LL header syntax, like the existing
three adapters. No owned GNU syntax, new C11 scope or compatibility exception
is authorized. See [manual review](firmware/drivers/hc_sr04/INTEGRATION.md).

## Configuration core vocabulary

`cfgbytes_`, `cfgwire_`, `cfgstore_` and `cfgctrl_` belong to matching headers:
configuration byte encoding, wire framing, persistent records and controller.
CRC = cyclic redundancy check; NOR denotes flash programmed by clearing bits.
These modules use C99, serialized caller-owned storage and named callback types.
Wire/storage fields are explicit bytes, never native structure representations.
See [the manual review](firmware/src/config/TRANSFER.md).

## Admission and binding vocabulary

jsonscan, cfgjson, cfgplan, cfgdev, cfgbind and c3bind identify lexical preflight,
bounded JSON access, node planning, descriptor admission, instance binding and
C3 backend selection. Public prefixes match their paired headers. JSON means
JavaScript Object Notation. New modules use C99; no exception or language scope
is broadened. See [manual review](firmware/src/config/ADMISSION.md).

## USB/storage application vocabulary

`cfgpump`, `c3usb`, `c3store` and `c3app` identify the configuration transport
pump, C3 USB endpoint, C3 persistent partition adapter and application owner.
USB = Universal Serial Bus; ZLP = zero-length packet. Matching public prefixes
and headers remain mandatory. See the
[manual review](firmware/src/config/USB_STORAGE.md#manual-barr-c-review).

## Observation service vocabulary

`sampcache`, `statusmon` and `telemetry` identify the sample cache, periodic status
monitor and observation encoder; public prefixes match paired headers. They use
C99 and the existing shared guard. See the
[manual review](firmware/src/services/TELEMETRY.md#manual-barr-c-review).
No compatibility exception or approved language scope is broadened.

`oledview` identifies the OLED text renderer; it uses C99 and the existing
driver framebuffer. See [manual review](firmware/src/services/DISPLAY.md#manual-barr-c-review).

## Rule module vocabulary

`rulecalc` evaluates typed metric conditions; `cfgrule` admits configuration
rules. Public prefixes belong to matching headers. Both are C99, using copied
state and existing component/cache ownership. See the
[manual review](firmware/src/automation/README.md#manual-barr-c-review).
No approved exception, formatter version or language boundary changes.

## Discovery module vocabulary

`disci2c` collects bounded I2C observations; `cfgdisc` admits reviewed connection
presets and integrates discovery with the existing configuration controller.
`disctest` tests those boundaries. Public prefixes belong to matching headers;
`reg` means register. All new production modules use C99. The current firmware
image hash comes from the pinned SDK's existing application descriptor API.
No exception or language scope changes. See
[the manual review](firmware/src/discovery/README.md#manual-barr-c-review).

`discuart` collects bounded UART report evidence using the existing parser;
`uartdisc` tests that observer and its configuration boundary. They use C99,
named callbacks and the same serialized owner. No new deviation is introduced.

## Offline timing model vocabulary

`timtree` is the bounded timing-tree classifier template; `tim_main` is its host
parity test under `tests/ai`. Generated C retains the same public prefix/header.
These use C99 with no new deviation and are not linked into deployed firmware.
See [manual review](ai/OFFLINE_PIPELINE.md#c-review). Exported sources receive
explicit format/lexical/module checks even when generated under ignored `build/`.
