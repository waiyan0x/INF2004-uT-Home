# ESP32-S3 HAL port

This port maps the public HAL to the selected ESP32-S3 toolchain and board. It
owns pin configuration, peripheral handles, interrupt glue, monotonic timing,
non-volatile configuration storage, and USB serial transport.

The first-prototype resource map is loaded from configuration:

- I²C0: GPIO8 SDA, GPIO9 SCL;
- SPI2: GPIO11 MOSI, GPIO12 SCLK;
- UART1: GPIO16 RX, GPIO17 TX;
- OLED control: GPIO10 CS, GPIO13 D/C, GPIO14 reset;
- HC-SR04: GPIO4 Trigger, GPIO5 Echo.

No driver or service may include ESP32-S3-specific headers directly.
