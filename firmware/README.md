# Configuration-driven firmware

The platform selects supported device instances, wiring, behaviours and routing
through a validated node profile. Device identities stay in descriptors and
drivers; the runtime manages generic task and resource lifecycles. See
[PROJECT_BASELINE.md](../PROJECT_BASELINE.md) for the current contract.

The working tree contains host lifecycle transactions, bound component dispatch,
resource/restart metrics, queues, health and watchdog modules. Member 1's branch
is reconciled with the generic planner/registry in the
[runtime integration record](src/runtime/INTEGRATION.md). Native periodic tasks
and generic activation/recovery have board evidence. I2C/SPI/UART/pulse/PWM
adapters and sensor/output bindings are software/build verified; peripheral
hardware acceptance remains deferred. Independent target admission, checked
profile binding, USB/persistence software, the local configurator and generic
rule routing are implemented. The next checkpoint is physical and combined
fault/resource acceptance with the team; AI follows last.

## Intended flow

```text
node profile + reviewed catalogue + board/build capabilities
    -> complete validated resource plan
    -> instance-specific driver contexts and runtime tasks
    -> typed bounded queues
    -> configured consumers and health reporting
```

A display is optional. The example profiles demonstrate a mixed-protocol node,
two instances of one sensor driver, and a distance-only node with no display.
They use the same schema and generic host runtime.

## Module ownership

- `app/`: target startup and orchestration.
- `include/uthome/`: common component, registry and runtime APIs.
- `src/config/`: compiled registry, bounded target admission and transfer/storage.
- `src/runtime/`: generic lifecycle, native worker adapter and plan activation.
- `src/services/`: normalization and consumers of declared sample types.
- `src/messaging/`: bounded queue transport.
- `src/health/`: liveness, queue and driver failures.
- `drivers/`: device command/decoding implementations behind the HAL.
- `hal/`: protocol APIs and board implementations.
- `third_party/`: pinned dependencies and reviewed integration patches.

## Target setup

Keep the selected `third_party/esp32c3-mtk3/` submodule pristine. Follow
[SETUP_ESP32C3.md](SETUP_ESP32C3.md) for the pinned source, SDK qualification
candidate and recorded task-example build. The target manifest remains empty until
actual adapters are linked and verified; catalogue membership is insufficient.

Native UART ownership, the UART/ADC resource patch and board pin restrictions
remain deployment constraints. The wiring in `configs/README.md` is one example.
The target parser uses managed `espressif/cjson` 1.7.19~2, with bounded
input/nesting/allocation and independent semantic/resource validation. JSON
parsing alone does not make a configuration valid.

The configuration application now connects USB and flash to target admission
and checked activation. See [current contracts and deferred acceptance](src/config/USB_STORAGE.md).
