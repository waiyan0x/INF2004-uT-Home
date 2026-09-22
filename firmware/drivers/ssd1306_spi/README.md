# SSD1306 SPI display driver

This is the provisional driver for the prototype's 128×64 SPI OLED. Confirm the
controller marking on the physical module before implementation; replace this
driver name if the supplied display uses another controller.

The driver receives an SPI interface plus CS, D/C, and reset GPIOs. It exposes
initialise, clear, render, and fault-screen operations. Display formatting stays
in the display service so the driver remains a controller-specific pixel and
command transport.

SPI timeouts or a missing display publish a health event. Display failure must
not stop sensor acquisition or serial monitoring.
