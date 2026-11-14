# Peripheral access

The runtime-rule path selects transactions through generic HAL contracts. It
does not need a dedicated sensor driver to interpret each returned value.

| Maintained area | Purpose |
|---|---|
| `../hal/i2cbus.c`, `../hal/include/i2cbus.h` | Generic I2C transfers and bus ownership |
| `../hal/spibus.c`, `../hal/include/spibus.h` | Generic SPI transfers and chip-select ownership |
| `../hal/uartio.c`, `../hal/include/uartio.h` | Generic UART operations and port ownership |
| `../src/services/transaction/` | Validate rules and choose bounded I2C/SPI/UART operations |
| `generic_i2c/` | Existing configurable register-read profile component |
| `bmp388/`, `ssd1306_spi/`, `hlk_ld2450/` | Existing decoded sensor/display profiles for setup, diagnosis and recovery |
| `hc_sr04/`, `pwm_output/` | Existing optional GPIO profile capabilities |

Device-profile components remain registered with the configuration system. They
are separate from the raw transaction path; transactions currently require an
idle peripheral plan. Removing these profiles would also require changes to
admission, UI, recovery procedures and tests, so they remain intact.

Drivers do not create tasks, allocate queues or choose application behavior.
The application owner supplies reviewed configuration and serializes access.
See the [transaction contract](../src/services/transaction/README.md),
[HAL guide](../hal/README.md) and each device's integration guide for actual
transfer shapes, fixed wiring and remaining hardware checks.
