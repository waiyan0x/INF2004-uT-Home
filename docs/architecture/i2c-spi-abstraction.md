# I²C and SPI Communication Abstraction

Design note for the reusable I²C and SPI layer (Member 2). Covers the HAL
contracts, how devices attach through configuration, the port and test model,
and the decisions that need team agreement.

## Scope

This layer provides two protocol contracts, `hal/include/i2c.h` and
`hal/include/spi.h`, plus a shared status type in `hal/include/hal_status.h`.
Peripheral drivers talk only to these contracts, never to a vendor SDK
directly. Each contract has two implementations: the real ESP32-S3 port and a
host simulation used for unit tests.

In the first prototype this layer carries two devices: the BMP388 pressure and
temperature sensor over I²C, and the SSD1306-compatible OLED over SPI. The
addresses, pins and clocks for both come from the validated JSON profile, not
from compiled constants.

## Layering

```
JSON profile
    -> config manager: interface and pin allocation
        -> hal/include contracts (this layer)
            -> hal/ports/<board> implementation
        -> drivers/ (use only the contracts above)
```

Two rules hold this together and are enforced by review:

- A driver includes only `hal/include/` headers. It never calls ESP-IDF or any
  board API. This is what lets the BMP388 and OLED drivers run unchanged on the
  host simulation.
- No board-specific code lives outside `hal/ports/`. Pin numbers, controller
  indices and clocks arrive as data from the profile.

## Status and timeouts

Every operation returns `hal_status_t`, a single enum shared by I²C, SPI and the
other HAL contracts (`HAL_OK`, `HAL_ERR_INVALID_ARG`, `HAL_ERR_NOT_INIT`,
`HAL_ERR_TIMEOUT`, `HAL_ERR_NACK`, `HAL_ERR_BUS`, `HAL_ERR_UNSUPPORTED`,
`HAL_ERR_NO_RESOURCE`). Every blocking call takes an explicit `timeout_ms`, with
`HAL_NO_WAIT` and `HAL_WAIT_FOREVER` as the two sentinels.

This is what makes the health path work. In the fault scenario, disconnecting
the BMP388 surfaces as `HAL_ERR_TIMEOUT` from the read, which the sensor service
turns into a health event and the OLED shows, without blocking the other tasks.

## I²C contract

One bus is shared by several devices. The bus is brought up once from its
interface config; each transaction then names the target with a 7-bit address.

- `hal_i2c_config_t`: `port`, `sda_gpio`, `scl_gpio`, `clock_hz`.
- Operations: `hal_i2c_write`, `hal_i2c_read`, and `hal_i2c_write_read`. The last
  is a repeated-start register read (write the register index, read its value
  without releasing the bus), which is the access pattern a register device like
  the BMP388 uses for its chip-id and data reads.

The bus handle is opaque and backed by static storage in the port, so no heap is
used. This matches the project rule on bounded, fixed resources.

## SPI contract

The bus is shared and each device adds its own chip select and mode, so the
contract is split in two: bring up a bus, then attach devices to it.

- `hal_spi_config_t`: `host`, `sclk_gpio`, `mosi_gpio`, `miso_gpio`, `clock_hz`.
  `miso_gpio` may be `HAL_GPIO_NONE` for write-only devices such as the OLED;
  a transfer that asks to read on such a bus returns `HAL_ERR_UNSUPPORTED`.
- `hal_spi_device_config_t`: `cs_gpio`, `mode`, `cs_active_low`, per-device
  `clock_hz`.
- Operations: `hal_spi_device_add`, then `hal_spi_transfer` (full duplex, either
  buffer may be null) and `hal_spi_write` (write-only convenience). The port
  asserts and releases chip select around each transfer. `hal_spi_device_remove`
  detaches a device and frees its chip select, so a peripheral can be disabled
  at runtime; `hal_spi_deinit` removes any devices still attached.

The OLED's data/command and reset lines are ordinary GPIOs, not part of SPI.
They belong to the GPIO contract (Member 3). The SSD1306 driver takes them as
two callbacks in its config rather than including the GPIO contract, and the
runtime binds each callback to a GPIO output. That keeps the driver free of a
dependency on another member's header and lets the host tests record every
D/C change.

## Address, chip-select and concurrency rules

Addressing is per transaction on I²C (`addr7`) and per device on SPI
(`cs_gpio`). The ports guard those assignments with `HAL_ERR_INVALID_ARG`:

- a zero `clock_hz`, or a controller (`port` / `host`) brought up twice;
- an SPI `mode` above 3, or a `cs_gpio` already used as a chip select.

The JSON validator (Member 5) should reject these first; the port check is the
last line of defence.

Devices on one bus may be driven from different tasks, for example the BMP388
and a light sensor on `i2c0`. The port holds a per-bus lock for each
transaction, so each call is atomic on the wire. Bring-up and teardown (init,
deinit, device add and remove) are not thread-safe and run from one task.

## Configuration mapping

The first prototype profile drives the layer as follows.

| Profile field | Contract field |
| --- | --- |
| interface `i2c0`: port, sda_gpio 8, scl_gpio 9, clock_hz 400000 | `hal_i2c_config_t` |
| device `bmp388`: interface.address 0x76 | `addr7` on each transaction |
| interface `spi2`: host 2, mosi_gpio 11, sclk_gpio 12, clock_hz 8000000 | `hal_spi_config_t` |
| device `oled`: cs_gpio 10 | `hal_spi_device_config_t.cs_gpio` |
| device `oled`: dc_gpio 13, reset_gpio 14 | GPIO outputs bound to `ssd1306_config_t.dc` / `.reset` |
| device `oled`: display width_px 128, height_px 64 | `ssd1306_config_t.width_px` / `.height_px` |

## Ports

- `hal/ports/esp32s3/`: the real implementation on the ESP32-S3 SDK. Not yet
  written. It is held behind the firmware implementation gate until the approved
  kernel port, toolchain and startup model are confirmed. Only these files
  change when the target is settled; nothing above them moves.
- `hal/ports/host_sim/`: fakes used by the unit tests. The I²C fake models each
  device as a register file, so a register-based driver can be exercised end to
  end. The SPI fake captures written bytes so a test can assert the exact
  command sequence a driver produced. Both can inject NACK, timeout and bus
  faults to drive error handling.

## Testing

Unit tests run on the host with plain gcc, no hardware.

- `tests/unit/test_hal.c`: contract-level tests for both buses, including the
  register-read and write-then-read paths, absent-device NACK, forced timeout,
  full-duplex SPI, configuration rejection (zero clock, duplicate controller,
  bad mode, duplicate chip select), and device removal.
- `tests/unit/test_bmp388.c`: the BMP388 driver over the Bosch API over the I²C
  fake, covering chip-id verification, the sampling registers written at init,
  rejection of a second init of the same sensor, absent-device NACK, and a full
  read, plus a mid-run disconnect that must surface as `HAL_ERR_TIMEOUT` /
  `HAL_ERR_NACK`.
- `tests/unit/test_ssd1306.c`: the OLED driver over the SPI fake. A fake GPIO
  line records each D/C change against its position in the captured byte
  stream, so the tests check the exact init sequence, that commands go out with
  D/C low and the frame with D/C high, the framebuffer layout, the font and
  clipping, the fault view and its word wrap, and SPI and D/C failures.

On-target checks live under `tests/hardware/` and wait on the board.

## BMP388 driver

The BMP388 driver is a thin adapter over the Bosch BMP3_SensorAPI, vendored
unmodified in `third_party/bmp3/` (v2.0.6, BSD-3-Clause). The vendor code owns
register access and compensation; the driver supplies the read, write and delay
callbacks onto the I²C contract, applies a periodic sampling configuration, and
returns an `environment_sample` (pressure in pascals, temperature in degrees
Celsius, a status, and a timestamp the runtime fills on publish). The delay
callback is a no-op on the host and must be wired to the time source on the
ESP32-S3 port before hardware bring-up.

Two adapter details matter for correctness:

- The Bosch driver passes multi-register writes interleaved as register/data
  pairs. The adapter sends each pair as its own two-byte write, which any I²C
  register device accepts, and drops the uninitialised byte that v2.0.6 appends.
- Bosch reports every bus failure as `BMP3_E_COMM_FAIL`. The adapter records
  the HAL status behind it and returns that, so a disconnect reads as
  `HAL_ERR_TIMEOUT` or `HAL_ERR_NACK` rather than a generic `HAL_ERR_BUS`. A
  chip that acknowledges with the wrong id returns `HAL_ERR_UNSUPPORTED`.

## SSD1306 driver

The OLED driver in `drivers/ssd1306_spi/` is a pixel and command transport. It
keeps a framebuffer in the controller's own layout (one byte per column per
8-row page), offers pixel and 5x7 text drawing, and sends the whole frame in
one transfer on `ssd1306_render()`. What appears on screen, and how often, is
the display service's job (`src/services/display/`); the driver never picks
content. The one fixed layout it owns is the fault view
(`ssd1306_show_fault()`): "FAULT", a
rule, and the caller's message wrapped at word boundaries.

- Init pulses reset (if wired), sends the standard sequence for a 3.3 V module
  (internal charge pump, horizontal addressing), blanks the panel memory, and
  only then switches the display on, so no garbage flashes at power-up. It
  supports 128x64 (the prototype) and 128x32.
- The SPI bus is write-only for this panel, so nothing can confirm the display
  is present. The driver reports what it can see: SPI transfer failures and
  D/C line failures, returned as the HAL status. An unplugged panel is not
  detectable.
- A failed render keeps the framebuffer, so the next render retries the same
  frame. Released handles return `HAL_ERR_NOT_INIT`.
- The SSD1306 accepts SPI mode 0 up to 10 MHz; the prototype's 8 MHz bus is
  within that. An SH1106 module (a common look-alike) needs a different init
  sequence and a 2-column offset, so check the controller marking on the
  module before hardware bring-up.

## Open decisions

These affect other members and should be agreed before merge.

1. `hal_status.h` is shared by every HAL contract, so a change to it touches the
   whole team. It needs sign-off, not a unilateral edit.
2. `environment_sample` currently lives in `include/uthome/sensor.h`. The sensor
   service and queue types share that space, so its final field set is a joint
   decision with Members 1 and 5.
3. Profile schema (Member 5, `system-integration` branch): it carries
   `miso_gpio` but not the SPI device `mode`, so the runtime should default
   mode to 0. For these drivers the schema should also limit the OLED to
   128x64 or 128x32 (it allows 1-1024), cap the OLED's SPI clock at 10 MHz (it
   allows 40 MHz), and restrict the SPI host to 2-3 (host 1 serves the flash
   on the ESP32-S3). A duplicate I²C port, SPI host or I²C address is not
   checked there; the HAL and BMP388 driver reject those.
4. The SSD1306 driver takes its data/command and reset lines as callbacks
   (`hal_status_t set(void *ctx, bool high)`). The runtime (Member 1) binds
   them to GPIO outputs from Member 3's contract, which needs a plain
   "set output level" call with a status return.
5. The root README device table still lists the earlier device set. It should be
   updated to the current drivers (bmp388, ssd1306_spi, hlk_ld2450, hc_sr04).
6. `drivers/README.md` expects drivers to expose their entry points through the
   component registry (`component.h`), which does not exist yet. Both drivers
   already have the init / operate / shutdown shape and return `hal_status_t`,
   so registering them is a thin wrapper once the registry is agreed.

## References

- Bosch Sensortec. (n.d.). *BMP3_SensorAPI* (v2.0.6) [Computer software]. GitHub.
  https://github.com/boschsensortec/BMP3_SensorAPI
- Bosch Sensortec. (n.d.). *BMP388 datasheet* (BST-BMP388-DS001).
  https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp388/
