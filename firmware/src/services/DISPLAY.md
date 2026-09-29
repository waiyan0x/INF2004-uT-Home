# Configured OLED display: checkpoint 6b

`oledview` is one small C99 renderer for the existing 128x64 SSD1306 framebuffer.
It adds no graphics dependency, heap allocation, second framebuffer or worker
framework. The existing driver, SPI/GPIO backends and activation lifecycle are
reused. Configuration selects wiring, source queues and refresh period; neither
runtime nor renderer names prototype equipment.

## Rendering and ownership

`shared_sample` copies one registered source and its time reference under the
existing short shared guard. Selection rotates every two seconds through all
sources on the configured queue mask, including multiple producers on one queue.
Rendering and SPI happen after releasing the guard. No queue item is consumed.
The current single-consumer admission reservation remains conservative; this
checkpoint does not change the planner's routing policy.

Each page shows the device index (or health queue), a shortened uppercase label,
and MISSING/FRESH/STALE/FAULT. Values include pressure/temperature, distance/echo,
all three radar slots with signed coordinates/velocity, or historical health
event/count. Health reports do not certify current node health. Missing/faulted
values clear old numbers; an unavailable snapshot shows WAITING FOR DATA.
The built-in 5x7 alphabet has 41 glyphs; unsupported label characters are blank.
Temperature uses one truncated decimal; telemetry retains the original values.

The binder opens shared SPI only for enabled displays, then claims each CS,
D/C and reset through existing ownership checks. Its worker renders directly
into that instance's frame and calls the existing bounded flush/retry adapter.
Only that worker writes the frame. Stopped-worker cleanup releases the driver,
control lines, SPI device, then shared bus. Partial opens and failed releases
retain handles for checked recovery; admission does not touch live resources.

The display context occupies the existing fixed slot union. This uses more
static memory even for sensor-only profiles, but keeps one ownership model and
avoids a separate frame allocator. Measured ELF sizes are recorded below.

## Manual BARR-C review

- Rule 1: C99, bounded and documented casts, paired types at callback boundaries.
- Rule 2: accurate ownership, rendering limits and error/retry contracts.
- Rule 3: pinned formatting and the complete lexical gate.
- Rule 4: one cohesive renderer, paired header/prefix and private helpers.
- Rule 5: fixed-width samples/masks, unsigned bounded shifts, matching integer
  printf macros and finite range checks before float-to-integer conversions.
- Rule 6: bounded functions/loops and explicit worker/lifecycle ownership;
  no added allocator, recursion, ISR or synchronization framework.
- Rule 7: initialized buffers/handles, stable callback contexts, copied samples
  and preserved ownership after failures; stopped workers precede union reuse.
- Rule 8: checked error paths, bounded formatting/glyph writes and active test
  assertions. The shared guard is released before formatting, GPIO or SPI.

The new module uses C99. Existing C11/GNU99 scopes, exceptions, approval,
checker policy, dependency pins and formatter version are unchanged. Software
review does not establish physical timing, stack peaks or complete compliance
with every semantic rule on hardware.

## Acceptance boundary

Existing host fixtures cover source selection, pixel output, missing/stale/fault
states, signed/extreme values, retry, partial setup and retained cleanup failures.
The original prototype still requests reserved GPIO2 for CS and remains rejected;
a valid independent OLED profile is accepted. No hardware profile was guessed
or rewritten, and the target-pending capability manifest remains empty.

Panel/controller identity, wiring, supply, orientation, SPI timing, combined
load and memory high-water checks remain checkpoint 8. SPI success cannot prove
panel presence. No board access or flash occurs here. Next is checkpoint 6c:
the local configuration/monitoring application; AI stays last.

## Verified software and memory

All 140 host tests pass (54 standard, 26 foundation, 18 runtime, 42 target),
including the extended existing display, binding and service fixtures.
The BARR-C gate covers 176 files with zero findings/tool-policy errors.
Both target modes pass with 118 inputs and 49 owned compiler profiles.
See [fresh build/source evidence](../../qualification/esp32c3-display-20260928.json).

The application ELF gives the binder 12,640 bytes, up from
5,976 because every fixed slot can hold a display frame.
The same six application/cache objects total 50,920 bytes;
the new eight GPIO output handles use another 16 bytes.
Other runtime objects, task stacks and heap remain additional. This is static
accounting, not measured free RAM or an AI capacity promise. The partition table
is unchanged from checkpoint 6a.
