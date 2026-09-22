#ifndef UTHOME_HAL_SPI_H
#define UTHOME_HAL_SPI_H

#include <stddef.h>
#include <stdint.h>
#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPI bus and device abstraction.
 *
 * A bus (e.g. "spi2") is shared; each device adds its own chip select and
 * mode. This mirrors the "spi" interface object (host, mosi_gpio, sclk_gpio,
 * clock_hz) plus the device-level cs_gpio in a node profile.
 *
 * Note: the D/C and reset lines used by the OLED are ordinary GPIOs and belong
 * to the GPIO HAL, not here. An SSD1306 driver takes an SPI device from this
 * contract plus GPIO handles from the GPIO contract.
 */

#define HAL_GPIO_NONE (0xFFu)  /* Sentinel for an unused optional line */

typedef struct {
    uint8_t  host;       /* SPI controller index, e.g. 2 for "spi2" */
    uint8_t  sclk_gpio;  /* Clock GPIO */
    uint8_t  mosi_gpio;  /* Controller-out line */
    uint8_t  miso_gpio;  /* Controller-in line, HAL_GPIO_NONE if write-only */
    uint32_t clock_hz;   /* Default bus clock, e.g. 8000000 */
} hal_spi_config_t;

typedef struct {
    uint8_t  cs_gpio;       /* Chip select for this device */
    uint8_t  mode;          /* SPI mode 0..3 (CPOL/CPHA); the SSD1306 uses 0 */
    uint8_t  cs_active_low; /* Non-zero if CS asserts low (the usual case) */
    uint32_t clock_hz;      /* Per-device clock, 0 to inherit the bus default */
} hal_spi_device_config_t;

/* Opaque handles, both backed by static storage in the port. */
typedef struct hal_spi_bus    hal_spi_bus_t;
typedef struct hal_spi_device hal_spi_device_t;

/* Bring up an SPI bus from a validated configuration. */
hal_status_t hal_spi_init(const hal_spi_config_t *config, hal_spi_bus_t **out_bus);

/* Release the bus after all its devices have been removed. */
hal_status_t hal_spi_deinit(hal_spi_bus_t *bus);

/* Attach a device (chip select + mode) to a bus. */
hal_status_t hal_spi_device_add(hal_spi_bus_t *bus,
                                const hal_spi_device_config_t *config,
                                hal_spi_device_t **out_device);

/*
 * Full-duplex transfer of len bytes for one device. tx may be NULL to clock
 * out zeros; rx may be NULL to discard input (write-only devices such as the
 * OLED). The port asserts CS for the transfer and releases it afterwards.
 */
hal_status_t hal_spi_transfer(hal_spi_device_t *device,
                              const uint8_t *tx, uint8_t *rx,
                              size_t len, uint32_t timeout_ms);

/* Convenience write-only transfer (input discarded). */
hal_status_t hal_spi_write(hal_spi_device_t *device,
                           const uint8_t *tx, size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_HAL_SPI_H */