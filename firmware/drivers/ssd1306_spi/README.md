# SSD1306 SPI display driver

The software provides caller-owned 128x32/128x64 framebuffers and bounded command
transport through the reusable SPI interface. Each instance borrows one SPI
device and exclusive D/C/reset outputs. It contains no pin numbers, tasks, text
layout or sensor-specific presentation.

`oledinst` binds the driver to the registry and periodic worker. Failed refreshes
retain dirty pixels; failed cleanup retains ownership. See the
[integration guide](INTEGRATION.md) for setup, limits, source attribution, manual
C review and pending acceptance. The [display service](../../src/services/DISPLAY.md)
now renders cached configured sources directly into the same framebuffer.

Hardware tests are deferred to the final stage. Controller identity and power
still need confirmation. SPI completion cannot detect an unplugged SSD1306:
its serial interface has no acknowledgement.
