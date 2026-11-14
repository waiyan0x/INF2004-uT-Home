# Project documentation

Start with the [current project direction](../README.md#resume-next-session)
and [team responsibilities](../CONTRIBUTING.md). Each maintained source folder
keeps its own implementation guide and contracts.

- [Firmware setup and module map](../firmware/README.md),
  [shared contracts](../firmware/CONTRACTS.md) and
  [application entry point](../firmware/app/README.md).
- [Configuration Desk](../tools/configurator/README.md),
  [USB transfer tools](../tools/config_transfer/README.md) and
  [build/qualification tools](../tools/target/README.md).
- [Networking](../firmware/src/network/README.md) and
  [runtime transactions](../firmware/src/services/transaction/README.md).
- [Hardware acceptance and scoped UART/GPIO guides](hardware/README.md).
- [Software tests](../tests/README.md) and
  [GitHub Actions](github-actions.md).
- [Peripheral descriptions](../datasheets/README.md) and
  [transaction tools](../tools/transaction/README.md).

The discovery workflow below remains the guide for identifying supported
devices on reviewed connections. Current implementation status belongs in the
source-folder guides. Store new build and bench results under ignored `build/`.

<a id="device-discovery-and-documentation"></a>

## Device discovery and documentation

Discovery answers which supported device is present on a **reviewed connection**.
Firmware gathers bounded observations; the computer matches them to catalogue
facts. This runs on request and does not scan arbitrary GPIOs or infer electrical
wiring. It is a setup aid, separate from runtime transaction rules.

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

For the tested BMP388, 500 ms / 100 ms timeout are bench starting values. For the
tested OLED, 250 ms / 20 ms timeout / 4 MHz SPI are bench starting values. They
are not universal optima. Details: [hardware guide](../HARDWARE_ACCEPTANCE.md).

## Implementation and checks

- [Firmware observer, exact wire format and manual C review](../firmware/src/discovery/README.md).
- [Configuration Desk workflow](../tools/configurator/README.md).
- [Host USB client](../tools/config_transfer/README.md).
- [Tests](../tests/README.md) and [hardware acceptance](../HARDWARE_ACCEPTANCE.md).
  Validate the selected image and actual wiring before claiming physical acceptance.
