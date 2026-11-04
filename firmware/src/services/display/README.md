# Display service

`oledwork.c/h` owns the recurring observation/render/flush sequence and its
fixed display context. `cfgbind` supplies settings, opens the bus and control
lines, and wires the worker and cleanup callbacks. `oledview.c/h` renders into
the existing SSD1306 frame; no extra framebuffer or task is allocated.

The worker copies observations under the shared guard and releases it before
SPI I/O. Contention renders unavailable data so a stale FRESH label cannot
persist indefinitely. Existing retry and flush deadlines remain unchanged.
See [contracts, review and acceptance limits](../TELEMETRY.md#oled-rendering).

Manual BARR-C review: C99, named interfaces, unchanged context field layout and
callback lifetimes, no casts or new allocation. One worker owns each context;
binding changes require stopped workers. Host binding/renderer tests cover the
same frame bytes, contention behavior and failed-flush retry.
