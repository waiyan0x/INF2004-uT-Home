# SSD1306 SPI display driver

Driver for the prototype's 128×64 SPI OLED (128×32 also supported). Confirm
the controller marking on the physical module before hardware bring-up: an
SH1106 look-alike needs a different init sequence.

The driver receives an SPI device (bus plus CS, mode 0) and the D/C and reset
lines as callbacks, which the runtime binds to GPIO outputs. It exposes
initialise, clear, pixel and 5×7 text drawing, render, power on/off, a fixed
fault screen, and shutdown. Display formatting stays in the display service,
so the driver remains a controller-specific pixel and command transport.

SPI to this panel is write-only, so the driver cannot tell whether the display
is attached. It returns SPI transfer and D/C line failures as a HAL status,
which the display service publishes as a health event. Display failure must
not stop sensor acquisition or serial monitoring.

Tests: `tests/unit/test_ssd1306.c`, host-only, against the SPI simulation.
