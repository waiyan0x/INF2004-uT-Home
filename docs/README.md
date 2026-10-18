# Device discovery and documentation

Discovery answers which supported device is present on a **reviewed connection**.
Firmware gathers bounded observations; the computer matches them to catalogue
facts. This runs on request and does not scan arbitrary GPIOs or infer electrical
wiring. It is separate from the experimental AI timing model.

## Supported paths

| Connection | Evidence | Result |
|---|---|---|
| I2C | Approved reads of register 0 at 0x76/0x77 | BMP388 ID 0x50 can match; other values remain unknown |
| UART | Bounded receive-only LD2450-format reports | Candidate requiring explicit module confirmation |
| SPI OLED | User confirms module, wiring and existing source queue | Manual identity; successful SPI transmission cannot prove panel presence |
| GPIO distance/PWM | User confirms actual module and electrical connection | Manual identity; PWM is added disabled |

The runtime/observer contains no device-name identification branches. New
identification rules belong in [the catalogue](../catalogue/README.md), provided
the existing observation method and compiled driver can support them. A new
protocol or driver still requires implementation and testing.

## Review and apply

1. Confirm module, power and wiring against the selected connection preset.
2. Request detection on an idle board with no configured/retained hardware.
   Disabled devices and declared buses still reserve wiring; discovery must not
   stop a working driver, toggle arbitrary pins or drain its UART reader.
3. Resolve identity, then check the running firmware's compiled capabilities.
4. Show the proposed settings with their sources. Unknown, ambiguous and
   unsupported results cannot produce an applicable candidate.
5. **Add to profile → Validate and review → Apply**. Adding preserves existing
   entries; full host and independent target admission still apply.

Identity states are unknown, candidate, ambiguous, verified or user-confirmed.
Manual confirmation is not electronic verification. Driver support is separate
from hardware qualification; an active profile is separate from current health.
Silence or a timeout cannot establish removal or erase a known identity.

Evidence is bound to client session, preset and configuration revision. Pending
reviews expire after 60 seconds and are invalidated by relevant edits, reconnects,
state changes or observed restarts. A client nonce/build hash is not a boot ID,
so an unobserved restart is not guaranteed to be detected. History is not current
connection evidence. Failed cleanup retains ownership and requires recovery;
there is no automatic retry or cancellation of an in-flight bounded probe.

## Where settings come from

| Setting | Authority |
|---|---|
| Protocol, pins, supply and signal voltage | Reviewed descriptor/preset and actual module confirmation |
| Address / UART format | Approved observations or fixed documented format |
| Driver and operations | Descriptor plus connected firmware capabilities |
| Initial reading/display interval | Existing valid setting, descriptor default or identified bench observation |
| Timeout, buffers, priority, stack, routing | Reviewed limits and existing planner/runtime rules |
| Future interval recommendation | Measured comparison/model within the same admission limits |

For the tested BMP388, 500 ms / 100 ms timeout are bench starting values. For the
tested OLED, 250 ms / 20 ms timeout / 4 MHz SPI are bench starting values. They
are not universal optima. Details: [hardware guide](../CONTRIBUTING.md).

## Implementation and checks

- [Firmware observer, exact wire format and manual C review](../firmware/src/discovery/README.md).
- [Configuration Desk workflow](../tools/configurator/README.md).
- [Host USB client](../tools/config_transfer/README.md).
- [Tests](../tests/README.md); D1-D4 software is implemented, physical discovery
  acceptance remains pending.

## Historical references

Repeated checkpoint diaries were consolidated on 2026-10-17. Current status and
remaining work are in [the project README](../README.md). Earlier text remains
in Git at commit `37319fbd55ca622779eca498519128e6a33bcaa5`:

```console
git show 37319fb:INTEGRATION_PLAN.md
git show 37319fb:ai/OFFLINE_PIPELINE.md
```

Dated qualification JSON, source hashes, dependency locks, raw logs and approval
records retain their original meaning and paths. A path stored in an old record
refers to that historical revision; it is not silently rewritten to certify
today's source. Current manual reviews and relocated guides are mapped below.


| Former guide | Current location |
|---|---|
| `INTEGRATION_PLAN.md` | [Guide](../README.md#remaining-work) |
| `PROJECT_BASELINE.md` | [Guide](../README.md#platform-contract) |
| `ai/OFFLINE_PIPELINE.md` | [Guide](../ai/README.md) |
| `ai/TIMING_EXPERIMENTS.md` | [Guide](../ai/README.md#prepare-an-experiment) |
| `ai/TRAINING_PIPELINE.md` | [Guide](../ai/README.md) |
| `configs/PROTOTYPE_REFERENCE.md` | [Guide](../configs/README.md#legacy-reference) |
| `docs/DEVICE_DISCOVERY.md` | [Guide](README.md) |
| `firmware/SETUP_ESP32C3.md` | [Guide](../firmware/README.md#pinned-target) |
| `firmware/drivers/bmp388/README.md` | [Guide](../firmware/drivers/bmp388/INTEGRATION.md) |
| `firmware/drivers/hc_sr04/README.md` | [Guide](../firmware/drivers/hc_sr04/INTEGRATION.md) |
| `firmware/drivers/hlk_ld2450/README.md` | [Guide](../firmware/drivers/hlk_ld2450/INTEGRATION.md) |
| `firmware/drivers/ssd1306_spi/README.md` | [Guide](../firmware/drivers/ssd1306_spi/INTEGRATION.md) |
| `firmware/src/config/ADMISSION.md` | [Guide](../firmware/src/config/README.md#admission) |
| `firmware/src/config/TRANSFER.md` | [Guide](../firmware/src/config/README.md#wire-version-1) |
| `firmware/src/config/USB_STORAGE.md` | [Guide](../firmware/src/config/README.md#transport-and-request-identity) |
| `firmware/src/runtime/ACTIVATION.md` | [Guide](../firmware/src/runtime/README.md#application-contract) |
| `firmware/src/runtime/INTEGRATION.md` | [Guide](../firmware/src/runtime/README.md#verification-and-limits) |
| `firmware/src/services/DISPLAY.md` | [Guide](../firmware/src/services/TELEMETRY.md#oled-rendering) |
| `firmware/src/services/README.md` | [Guide](../firmware/src/services/TELEMETRY.md) |
| `ut-home-workload-distribution.md` | [Guide](../README.md#team-responsibilities) |


Manual C review sections: [configuration](../firmware/src/config/README.md#verification-and-manual-c-review),
[runtime](../firmware/src/runtime/README.md#verification-and-limits),
[display](../firmware/src/services/TELEMETRY.md#display-c-review) and
[AI export](../ai/README.md#c-review). The signed [audit](../tools/c_standard/AUDIT.md)
and [approval records](../tools/c_standard/README.md) retain their own files.

## Local artifacts and review documents

The following are local files, not part of the published repository. Paths are
relative to the `Code` folder unless stated otherwise. Preserve them when
changing workspaces; a new chat does not need a new clone on this workstation.

| Location | State / purpose |
|---|---|
| `build/ai-offline-20261013/` | Simulated runs, dataset, readable trained model and C export; see the [AI guide](../ai/README.md#existing-local-model) |
| `build/publish-20261017-*.log` | Six Python suite logs: 243 tests; the seven dashboard tests made the reported total 250. These are historical checks, not reruns on 18 October |
| `build/ai-c-gate-final.json` | Retained 194-file full-gate result; policy and manual-review requirements remain in the [C standard](../C_CODING_STANDARD.md) |
| `../ESP/Tribunal1/Week8_Tribunal1_Template.docx` | Only Version A was edited to match uT-Home. Front matter and Versions B/C were preserved; this document was not pushed to GitHub |
| `build/tribunal_review_20261017/` | Original Word backup, edit script, scope verification and rendered layout checks |

**Tribunal follow-up:** the front-page Team Information table still contains the
template's unrelated personas because it was outside the requested Version A
scope. The user identified this mismatch; replacement roles were explained in
chat but have not been applied to the Word file. Use the
[actual team responsibilities](../README.md#team-responsibilities) when that
table is authorized for editing. Leave student details for the team to fill in.
Buddy prompts/positions in Version A are proposals, not invented records of
completed individual sessions. The team must confirm them and reconcile the
Week 6 comparisons with its submitted architecture freeze before submission.

On 18 October, the Word file was verified to match the saved Version A edit.
No edits to it or to its neighboring PDF were made during the handoff check.
