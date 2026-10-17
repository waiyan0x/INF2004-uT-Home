# Device discovery and recommended settings

Design contract **0.1.0**, 2026-10-12. This extends platform baseline 2.1.0.
Checkpoints D1/D2 supply reviewed catalogue policies, bounded I2C observations,
a USB command and PC-side identity matching. D3 adds the existing local UI's
Detect button, sourced settings and additive draft review. The new discovery
path still needs physical acceptance with the built C3 application.
Existing node, descriptor and board contracts keep their current versions.

The intended flow is **detect → identify → recommend → review → apply**.
Detection and protocol selection use evidence and reviewed facts. Later AI may
choose useful timing settings within those facts. No training is needed to
recognise the first supported devices.

## 1. Choose a known connection

A connection preset records a name/version, board capability reference, pins,
interface settings, supply/logic requirements and the permitted probe methods.
The user confirms that the actual wiring and module voltage requirements match.
It selects a connection, not necessarily an exact sensor model. Software cannot
discover unknown wiring, voltage tolerance or interface straps safely.

For the first implementation, use one selected connection at a time. These are
the starting presets to implement from the existing bench setup:

| Connection | Initial settings | Electrical/ownership requirement |
|---|---|---|
| `bench_i2c` | I2C0, SDA GPIO0, SCL GPIO1, 100 kHz | Reviewed 3.3 V supply/logic and pull-ups; only the approved 0x76/0x77 ID reads |
| `bench_uart` | UART1, ESP RX GPIO20, TX GPIO21, 256000 baud, 8 data bits, no parity, 1 stop bit | Reviewed LD2450 setup: 5 V supply, 3.3 V UART logic, common ground; receive only during discovery |
| `bench_oled` | SPI2, clock GPIO7, data GPIO6, reset GPIO10, D/C GPIO3, 4 MHz | Tested six-pin module at 3.3 V; sole SPI device; GPIO4 reserved but unwired; manual identification |

These are project examples, not permanent platform pin assignments. `bench_i2c`
is implemented and checked; UART/OLED presets remain design inputs. A new
preset requires board/pin checks and electrical review. GPIO devices use manual
selection and their existing reviewed profiles first.

Rewire with power off. Discovery is explicitly requested through the CLI or D3
UI while the hardware configuration is empty. Startup does not probe automatically.
The UI exposes **Detect devices**. This is not live electrical
hot-plug support. Restoring a saved profile does not silently replace its device
assignments with new discovery results.

## 2. Observe before identifying

The versioned policies are in [discovery.json](../catalogue/discovery.json).
They reference existing descriptors instead of copying protocol limits, driver
IDs, commands, resource limits or example defaults. The JSON file is data for
review, not an instruction interpreter that can perform arbitrary bus writes.

| Supported type | Evidence to collect | Result and limitation |
|---|---|---|
| BMP388 | One-byte read of register 0x00 at approved address 0x76 or 0x77; value 0x50 | `verified` chip-ID match; ACK alone stays `unknown`. 0x60 must not be labelled BMP388. Neither result verifies the breakout or supply. |
| LD2450 | At least 3 complete reports accepted by the existing parser in one error-free 1000 ms window | `candidate` with a compatible report format; user confirms model. Zero targets still counts as a valid response. |
| SPI OLED | User confirmation of model/compatible driver, straps and wiring; later visual display check | `user_confirmed`; successful writes alone cannot establish presence or exact controller identity. |
| HC-SR04 | User confirmation, then a separate reviewed trigger/echo test | `user_confirmed`; pulse activity alone is not a model ID. |
| PWM output | User confirmation of the load and its driver circuit | `user_confirmed` output capability; never energise an unknown load to discover it. |

The radar window is a starting policy to test, not a measured guarantee. There
is no universal SPI ID command: future readable SPI devices need their own
reviewed method and known wiring. New identities extend the catalogue; new probe
methods also need a bounded firmware implementation and tests.

Unrecognised responses remain `unknown`; multiple matching descriptors remain
`ambiguous`. Do not choose the first candidate or reuse a previous identity as
fresh evidence. A bus error is `fault`; a completed attempt without the expected
response is `not_responding`, never proof of physical removal.

## 3. Shared discovery record

Use one record per physical endpoint: an I2C connection plus address, one UART
connection, or one manually selected SPI/GPIO device connection. Two I2C
addresses produce separate records. An address collision cannot be reliably
distinguished by software and must be resolved at wiring review.

The following is the full workflow's record design. D2 implements the I2C
observation/identity subset; the D3 UI adds sourced settings from descriptors
and connection-preset policy. The standalone identity resolver remains unchanged.
The explicit
[USB format and implementation limits](../firmware/src/discovery/README.md)
are separate from this host record; no native C struct is serialized directly.

| Part | Required information |
|---|---|
| Context | `contract_version`, request ID, client-session nonce, configuration revision, discovery catalogue version, board capability reference and actual firmware build ID; the nonce is not a board boot ID |
| Connection | Preset ID/version, interface ID and endpoint address where applicable; resolve pins, supply and settings from the reviewed preset |
| Observation | Method ID, elapsed time and sample time within the session, `responding / not_responding / not_checked / busy / fault`, and bounded method-specific evidence |
| Identity | `unknown / candidate / ambiguous / verified / user_confirmed`; candidate descriptor ID/version references; selected reference only for verified/user-confirmed results; matching reason or explicit user confirmation |
| Capabilities | Resolved descriptor reference and `available / unavailable / unchecked` driver support from the current firmware's advertised registry; hardware qualification recorded separately |
| Recommendation | `awaiting_identity / blocked / awaiting_review`; proposed instance ID, setting values with units, reason and source per value (`descriptor_default / bench_observation / rule / measured / model`) |
| Operating status | `not_configured / running / not_responding / fault / unknown`, with its own sample time; a discovery result cannot assert successful activation |

An I2C observation retains address, register and returned byte. A UART observation
retains parser ID, valid report count, observation window and receive-error
count; do not add unbounded raw serial logs. Manual confirmation retains what
the user confirmed and when; it is not a fabricated hardware response.

Use bounded identifiers consistent with node IDs (31 characters), at most eight
endpoint records per request and at most eight candidate references per record.
If the result capacity is reached, report `incomplete` at request level; do not
report omitted endpoints as absent. Method timeouts and request deadline must be
explicit before a target probe is implemented. Incomplete, cancelled and busy
requests do not overwrite prior working assignments.

Host results are correlated to the client session, configuration revision and
preset version. The echoed nonce does not detect an unnoticed reboot by itself.
Disconnect/reboot, rewiring/preset changes or configuration
replacement invalidate pending reviews. Previous observations may remain in
history with their old timestamps, but are not current connection evidence.
Operating health uses the existing per-device freshness/timeout rules.

Until identity is selected, the selected descriptor and resolved capability
reference are null and recommendation settings are empty. Before a firmware
capability report is available, support stays `unchecked` and application is
blocked. A timeout does not clear the configured identity; it updates current
observation/operating status while preserving the prior identity's provenance.

## 4. Resolve the specifics from trusted sources

| Specific | Source and decision |
|---|---|
| Protocol | Selected wiring/straps plus reviewed descriptor. BMP388 uses I2C in the current binding; physical SPI support does not enable an unimplemented SPI path. |
| Pins and controllers | Reviewed connection preset checked against board capabilities and current resource ownership. Never assigned by the model. |
| Supply and signal voltage | Actual module specifications and reviewed wiring, confirmed by the user. A chip ID does not establish either. |
| Address or UART format | Observed approved address or descriptor's fixed serial format; do not guess settings until something responds. |
| Driver and operations | Descriptor plus the running firmware's compiled capabilities. A catalogue match can still be unsupported by the build. |
| Reading/refresh interval | Existing valid setting first, otherwise an explicitly sourced starting value; later rules/model may recommend within descriptor limits. |
| Timeout and buffer | Reviewed limits and measured workload; no AI changes initially. |
| Priority, stack and routing | Existing planner/runtime rules and explicit instance/queue choices. Retain current validated choices; no AI allocation initially. |

For a new BMP388 instance on the tested setup, the bench starting values are
500 ms reading interval and 100 ms timeout. For the tested OLED, they are
250 ms refresh, 20 ms transfer timeout and 4 MHz SPI. These differ from some
descriptor defaults. Label them `bench_observation`, not universal optimum or
automatically accepted values for another setup. See the existing
[hardware guide](../CONTRIBUTING.md). No descriptor limits are widened here.

An unknown/ambiguous identity gets no settings recommendation. A UART candidate
may display likely catalogue details but must await model confirmation before
producing an applyable candidate. A missing driver blocks application. Manual
identity alone does not claim the device has passed an operational test.

After review, construct an ordinary node profile and pass it through the existing
shared planner, local configurator review/apply flow and independent firmware
validation. A discovery record is never accepted as a node profile. Re-check
resources and session/revision before applying; `awaiting_review` is not proof
that the eventual complete profile will pass admission.

## 5. Keep ownership simple

The PC resolves catalogue details and presents the review. The board collects
bounded observations using compiled, allowlisted methods. No second bus owner,
background GPIO scan, arbitrary register scripting or AI hardware access is
introduced.

For D2, any configured hardware, active workers or retained bus ownership returns
`busy`. Even disabled devices and unused declared interfaces reserve wiring.
It must not stop a working device, reset a shared bus, drain a live UART reader
or temporarily toggle output pins. A later observation path may reuse data from
the existing owning driver, but must use its evidence and freshness rules.
All acquired resources must be released on success and timeout; failed release
retains ownership and blocks activation until explicit recovery. A disconnected
USB client does not interrupt a running synchronous probe; its bounded cleanup
still completes. There is no cancellation command in D2.

## 6. Implementation checkpoints

1. **D1 — implemented:** settle the record, reviewed identification policies,
   sources and protocol/settings ownership; validate the catalogue contract.
2. **D2 — generic I2C, implemented in software:** a reviewed preset selects bounded
   register reads; the core returns raw observations without model names. USB
   carries that evidence to catalogue matching on the PC. BMP388 is the first
   identity rule. Host tests and target builds are recorded separately from the
   still-pending physical discovery acceptance.
3. **D3 — local review, implemented in software:** the existing Configuration Desk
   shows observations, identity, driver availability and sourced specifics.
   Unknown/unsupported devices stay explicit. Single-use, 60-second detection
   tokens permit an additive draft candidate through the existing whole-profile
   admission/review/apply flow; no working entries are overwritten. Reconnects,
   edits, board state changes and observed restarts invalidate pending evidence.
   See [UI workflow and reset limitation](../tools/configurator/README.md#device-discovery-d3).
4. **D4 — radar/manual devices, implemented in software:** receive-only radar
   candidates require model confirmation. OLED/GPIO use explicit manual selection;
   OLED selects an existing readings queue and PWM is added disabled. Host tests
   cover noisy/empty/zero-target reports, retained resources, source selection,
   stale confirmations and conflicts. The approved additions retain the existing
   UI style and ordinary review/apply path. See the
   [D4 guide](../tools/configurator/README.md#radar-and-manual-devices-d4).
5. **AI — afterwards:** collect measured scenarios and compare useful timing
   recommendations against defaults/rules before training. Detection, protocol,
   wiring and driver availability remain deterministic.

The partial hardware acceptance results remain partial, especially radar
disconnect/fault behaviour, outputs and combined timing/memory measurements.
D3/D4 have host and simulated-browser checks; physical discovery still needs acceptance.
No new teammate input was needed for these software steps; new physical modules will need their
exact model, wiring and supply.
