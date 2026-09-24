/*
 * Host simulation of the SPI contract (firmware/hal/include/spi.h).
 *
 * Each device captures the bytes written to it so a test can verify the exact
 * command and data sequence a driver produced (for example an OLED init and
 * render sequence). Full-duplex reads return a test-set MISO pattern. Faults
 * can be forced to exercise driver error handling. Timeout semantics mirror
 * what the esp32s3 port must provide.
 */

#include "spi.h"
#include "host_sim.h"

#include <string.h>

#define SPI_MAX_BUSES     4
#define SPI_MAX_DEVICES   8
#define SPI_CAPTURE_BYTES 2048 /* A full 128x64 OLED frame (1024) plus commands */

struct hal_spi_bus {
    bool             in_use;
    bool             initialised;
    hal_spi_config_t cfg;
};

struct hal_spi_device {
    bool                    in_use;
    struct hal_spi_bus     *bus;
    hal_spi_device_config_t cfg;
    uint8_t                 capture[SPI_CAPTURE_BYTES];
    size_t                  capture_len;
    uint8_t                 miso_pattern;
    hal_status_t            fail_next; /* HAL_OK means no forced fault pending */
};

static struct hal_spi_bus    g_buses[SPI_MAX_BUSES];
static struct hal_spi_device g_devices[SPI_MAX_DEVICES];

void host_sim_reset_spi(void)
{
    memset(g_buses, 0, sizeof(g_buses));
    memset(g_devices, 0, sizeof(g_devices));
}

hal_status_t hal_spi_init(const hal_spi_config_t *config, hal_spi_bus_t **out_bus)
{
    if (config == NULL || out_bus == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    *out_bus = NULL;
    if (config->clock_hz == 0) {
        return HAL_ERR_INVALID_ARG;
    }

    struct hal_spi_bus *slot = NULL;
    for (int i = 0; i < SPI_MAX_BUSES; ++i) {
        if (g_buses[i].in_use) {
            if (g_buses[i].cfg.host == config->host) {
                return HAL_ERR_INVALID_ARG; /* Controller already brought up */
            }
        } else if (slot == NULL) {
            slot = &g_buses[i];
        }
    }
    if (slot == NULL) {
        return HAL_ERR_NO_RESOURCE;
    }

    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->initialised = true;
    slot->cfg = *config;
    *out_bus = slot;
    return HAL_OK;
}

hal_status_t hal_spi_deinit(hal_spi_bus_t *bus)
{
    if (bus == NULL || !bus->in_use) {
        return HAL_ERR_NOT_INIT;
    }
    /* Remove any devices still attached so their handles stop working. */
    for (int i = 0; i < SPI_MAX_DEVICES; ++i) {
        if (g_devices[i].in_use && g_devices[i].bus == bus) {
            memset(&g_devices[i], 0, sizeof(g_devices[i]));
        }
    }
    memset(bus, 0, sizeof(*bus));
    return HAL_OK;
}

hal_status_t hal_spi_device_add(hal_spi_bus_t *bus,
                                const hal_spi_device_config_t *config,
                                hal_spi_device_t **out_device)
{
    if (config == NULL || out_device == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    *out_device = NULL;
    if (bus == NULL || !bus->in_use || !bus->initialised) {
        return HAL_ERR_NOT_INIT;
    }
    if (config->mode > 3) {
        return HAL_ERR_INVALID_ARG;
    }

    struct hal_spi_device *slot = NULL;
    for (int i = 0; i < SPI_MAX_DEVICES; ++i) {
        if (g_devices[i].in_use) {
            if (g_devices[i].cfg.cs_gpio == config->cs_gpio) {
                return HAL_ERR_INVALID_ARG; /* GPIO already a chip select */
            }
        } else if (slot == NULL) {
            slot = &g_devices[i];
        }
    }
    if (slot == NULL) {
        return HAL_ERR_NO_RESOURCE;
    }

    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->bus = bus;
    slot->cfg = *config;
    slot->fail_next = HAL_OK;
    *out_device = slot;
    return HAL_OK;
}

hal_status_t hal_spi_device_remove(hal_spi_device_t *device)
{
    if (device == NULL || !device->in_use) {
        return HAL_ERR_NOT_INIT;
    }
    memset(device, 0, sizeof(*device));
    return HAL_OK;
}

hal_status_t hal_spi_transfer(hal_spi_device_t *device,
                              const uint8_t *tx, uint8_t *rx,
                              size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (device == NULL || !device->in_use) {
        return HAL_ERR_NOT_INIT;
    }
    if (len > 0 && tx == NULL && rx == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    if (rx != NULL && device->bus->cfg.miso_gpio == HAL_GPIO_NONE) {
        return HAL_ERR_UNSUPPORTED; /* Write-only bus: nothing to read from */
    }
    if (device->fail_next != HAL_OK) {
        hal_status_t forced = device->fail_next;
        device->fail_next = HAL_OK;
        return forced;
    }

    for (size_t i = 0; i < len; ++i) {
        uint8_t out = (tx != NULL) ? tx[i] : 0x00;
        if (device->capture_len < SPI_CAPTURE_BYTES) {
            device->capture[device->capture_len++] = out;
        }
        if (rx != NULL) {
            rx[i] = device->miso_pattern;
        }
    }
    return HAL_OK;
}

hal_status_t hal_spi_write(hal_spi_device_t *device,
                           const uint8_t *tx, size_t len, uint32_t timeout_ms)
{
    return hal_spi_transfer(device, tx, NULL, len, timeout_ms);
}

/* --- test controls --- */

void hal_spi_sim_get_capture(hal_spi_device_t *device, const uint8_t **out_buf, size_t *out_len)
{
    if (device != NULL && out_buf != NULL && out_len != NULL) {
        *out_buf = device->capture;
        *out_len = device->capture_len;
    }
}

void hal_spi_sim_clear_capture(hal_spi_device_t *device)
{
    if (device != NULL) {
        device->capture_len = 0;
    }
}

void hal_spi_sim_set_miso(hal_spi_device_t *device, uint8_t pattern)
{
    if (device != NULL) {
        device->miso_pattern = pattern;
    }
}

void hal_spi_sim_fail_next(hal_spi_device_t *device, hal_status_t status)
{
    if (device != NULL) {
        device->fail_next = status;
    }
}
