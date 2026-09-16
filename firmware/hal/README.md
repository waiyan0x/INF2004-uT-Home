`include/`: GPIO, I²C, SPI, time, storage, network APIs.
`ports/esp32s3/`: ESP32-S3 N16R8 implementation (the only board-specific code).
`ports/host_sim/`: fakes for host unit tests. Adding a new board = adding a new port.
