# Configuration admission and activation

Node schema 2.1.0, with 2.0.0 sensor/display compatibility, selects reviewed
capabilities and independent device instances. The host planner and target C
admission validate independently. See [the baseline](../../../PROJECT_BASELINE.md).

The pipeline is:

1. `cfgwire`/`cfgpump` receive one bounded CRC-framed USB request.
2. `cfgctrl` sequences admission, checked whole-plan activation and storage.
3. `cfgjson`/`cfgplan`/`cfgdev`/`cfgrule` reject malformed or conflicting profiles.
4. `cfgbind` copies driver/rule settings; `c3bind` supplies actual transports.
5. The generic runtime owns task/resource lifecycle; drivers own device state.
6. `cfgstore` retains independently validated persistent records in two slots.

`drvreg` binds compiled implementations to independently owned component
contexts. Profiles cannot install code, widen capabilities or introduce new
operations. Read-only telemetry and capabilities preserve mutation retry state.
The [local configurator](../../../tools/configurator/README.md) uses the shared
planner/client, rechecks reviewed state before Apply, and never automatically
retries an uncertain mutation.

Detailed contracts and manual review:

- [Wire/controller/storage](TRANSFER.md)
- [Independent admission and binding](ADMISSION.md)
- [USB/storage application](USB_STORAGE.md)
- [Display service](../services/DISPLAY.md)
- [Generic rules and PWM output](../automation/README.md)

Earlier checkpoint records retain their original scope and evidence. Physical
acceptance remains checkpoint 8, followed by AI at 9; the pending hardware
manifest remains empty even though compiled capabilities are now discoverable.
