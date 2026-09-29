# SPI and SSD1306 integration

This checkpoint adds reusable SPI, a C3 FIFO backend, shared pin ownership,
simple digital outputs and an independent SSD1306 component adapter. Physical
tests are deferred to final hardware acceptance at the user's request.

## Modules and provenance

- `hal/include/spibus.h`, `hal/spibus.c`: borrowed bus/device storage, distinct
  CS pins, driver claims, bounded transfers and absolute deadlines.
- `hal/include/pinpool.h`, `hal/pinpool.c`: validate complete pin sets before
  changing ownership; reject wrong-owner release.
- `hal/include/digiout.h`: exclusive, bounded output callback.
- `hal/ports/esp32c3/c3pin.c`: common C3 pin ledger and push-pull outputs.
- `hal/ports/esp32c3/c3spi.c`: SPI2 controller/mutex and FIFO transport.
- `ssd1306.c/.h`: caller-owned pixels, reset and controller commands.
- `oledinst.c/.h`: registry configuration, frame copies, flush, health, cleanup.
- `hal/ports/host_sim/spifake.c`: transaction/time/line failure fixture.

HAL paths above are relative to `firmware/`. I2C now uses the same pin ledger;
its transport contract is unchanged. The runtime contains no OLED branches.

The ownership model, page layout, reset and command flow adapt Jian Xuan's cached
work at `56f718e825674c70716f52d798e5bf011ebaa9f0` (`jx03-9028`). No vendor files
changed. The single-display pool and driver-local text/fault layout were not
imported; formatting belongs to a service. Commands were checked against the
[Solomon Systech SSD1306 Rev 1.1 datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf).
VCOMH uses documented `0x20` instead of the cached `0x40` value.

## Binding and lifecycle

1. Admit the complete profile against board, descriptor and compiled limits.
   Open one `c3spi` bus, then attach zeroed `spibus_device_t` contexts with
   distinct CS pins and explicit clocks/modes.
2. Open separate zeroed `c3pin_output_t` contexts for D/C and reset, initially
   low/high respectively. Expose `c3pin_set` through `digiout_t`. These pins
   cannot overlap I2C, SPI signals, another CS or another output.
3. Zero each `oledinst_t`; assign borrowed SPI, `data_line` and reset callbacks.
   Bind `*oledinst_descriptor()` through `drvreg_bind` using `ssd1306_spi`.
   Binding itself performs no I/O.
4. Copy `ssd1306_config_t` into activation settings and assign `oledinst_step`
   and `oledinst_cleanup`. The application still resolves scheduling/routing.
5. The display worker copies a full frame through `component_write`, then
   flushes it. One execution owner accesses each display; arbitrary concurrent
   writes from other tasks are unsupported. The production
   [display service](../../src/services/DISPLAY.md) renders directly into the
   same buffer and uses copied cache entries without draining queues.
6. After stopped-worker cleanup succeeds, detach devices, close outputs, then
   close the bus. Bus open/close requires zero owned workers. Claim changes are
   serialized lifecycle operations with affected users stopped, never ISR work.

Borrowed resources outlive cleanup/recovery. Failed initialization retains the
display claim. Close releases reset and transmits display-off before releasing
ownership; failures preserve the context for retry. The application also unwinds
resources opened before a rejected binding. Wiring changes require empty-plan
shutdown and controller reopening.

The catalogue describes a 128x64 queue-consuming service. The pixel driver also
supports 128x32 without expanding that catalogue profile. Map its 5..20 ms
`behaviour.timeout_ms` to the refresh/close budget; use a separate
`setup_timeout_ms` (initial recommendation: 100 ms) for reset plus blanking.
Driver ranges are 5..100 ms for operations and 20..100 ms for setup. Contrast,
remap and charge-pump choice are explicit panel policy. The initial module
assumption is an internal charge pump; other supplies require reviewed setup.

## Limits and errors

The portable bus supports four attached devices and 1..1024 bytes per call,
active-low CS and MSB-first bytes. A copied live device is not another handle.
Buffers are borrowed during synchronous calls only. NULL TX clocks zeros;
NULL RX discards input; both NULL is invalid. Failed RX contents are unusable.

C3 supports SPI2, modes 0..3, 100 kHz..8 MHz TX and optional MISO. RX is initially
limited to 1 MHz pending timing qualification. SDK clock rounding that would
exceed the request is rejected. One deadline includes mutex contention and all
64-byte FIFO chunks within one CS window. Padded temporary storage prevents the
SDK's word-copy helper from reading beyond short buffers. Register-update and
transfer waits have deadlines and kernel yields; the SDK's unbounded update-wait
helper is not used. Errors deassert CS and reset the controller before unlocking.
No DMA, interrupt handler or heap allocation was added.

C3 pins currently remain 0, 1, 3..7 and 10. UART20/21 belong to upstream startup;
flash, USB and straps are excluded. The board catalogue's conditional GPIO2 CS
policy is **not implemented by this backend**. Binding must reject it until the
pull-up/boot policy is qualified and explicitly added. Target capabilities remain
empty; no existing profile is silently advertised as deployable.

One driver operation shares a deadline across commands, data and reset delays.
Failed refreshes retain dirty pixels and resend the complete window/frame next
time. The component retains the error and reports timeout or device-I/O health
while permitting retries. Shared-lock contention may undercount health events.

SSD1306 serial mode has no readback/acknowledgement. An unplugged display can
therefore give successful SPI completion. Errors describe transport or callback
failures, not claimed presence detection. Close proves command transmission,
not panel receipt.

## Verification and manual C review

`tests/target/oledtest.c` covers pin conflicts/wrong-owner release, device identity,
duplicate CS, failed attach/detach, bounds, reset delays, command/data boundaries,
independent frames, coordinates, copied ownership, whole-operation timeouts,
complete retries and retained activation cleanup. The host kernel invokes steps
manually; it does not prove scheduling, native mutex contention or waveforms.

The target image compiles the real backends and adds argument checks without
opening a bus or touching outputs. Fresh evidence lives under
`firmware/qualification/`; earlier physical results retain their original scope.

Manual BARR-C review covers module boundaries, names, const/static, fixed-width
masks/shifts, bounded casts and padded buffers, initialization, aligned config
copies, retained owners, MMIO qualifiers, finite polling and cleanup errors.
Ordinary new source uses C99. C3 SPI/pin adapters require GNU99 only for excluded
SDK headers, as with C3 I2C; owned source introduces no GNU constructs or C11.
Policy, deviations and approval manifests remain unchanged.

Final hardware acceptance must confirm module identity/size/power, visible test
patterns, reset/CS/DC timing, SPI waveforms, sharing/contention, injected faults,
recovery and boot behaviour with all devices attached. Missing OLED hardware
requires visual/electrical observation, not an invented acknowledgement test.
