/*
 * Host simulation of the I2C contract (firmware/hal/include/i2c.h).
 *
 * Each simulated bus models its devices as a 256-byte register file addressed
 * by a 7-bit address. Writes set a register cursor and store following bytes;
 * reads and write-read return bytes from the cursor. This is enough to test a
 * register-based driver (chip-id check, block reads) without hardware. The
 * behaviour and timeout semantics mirror what the esp32s3 port must provide.
 */

#include "i2c.h"
#include "host_sim.h"

#include <string.h>

#define I2C_MAX_BUSES   4
#define I2C_ADDR_SPACE  128
#define I2C_REG_BYTES   256

struct hal_i2c_bus {
    bool             in_use;
    bool             initialised;
    hal_i2c_config_t cfg;
    bool             present[I2C_ADDR_SPACE];
    uint8_t          regs[I2C_ADDR_SPACE][I2C_REG_BYTES];
    uint8_t          cursor[I2C_ADDR_SPACE];
    hal_status_t     fail_next; /* HAL_OK means no forced fault pending */
};

static struct hal_i2c_bus g_buses[I2C_MAX_BUSES];

void host_sim_reset_i2c(void)
{
    memset(g_buses, 0, sizeof(g_buses));
}

hal_status_t hal_i2c_init(const hal_i2c_config_t *config, hal_i2c_bus_t **out_bus)
{
    if (config == NULL || out_bus == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    *out_bus = NULL;
    if (config->clock_hz == 0) {
        return HAL_ERR_INVALID_ARG;
    }

    struct hal_i2c_bus *slot = NULL;
    for (int i = 0; i < I2C_MAX_BUSES; ++i) {
        if (g_buses[i].in_use) {
            if (g_buses[i].cfg.port == config->port) {
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
    slot->fail_next = HAL_OK;
    *out_bus = slot;
    return HAL_OK;
}

hal_status_t hal_i2c_deinit(hal_i2c_bus_t *bus)
{
    if (bus == NULL || !bus->in_use) {
        return HAL_ERR_NOT_INIT;
    }
    memset(bus, 0, sizeof(*bus));
    return HAL_OK;
}

/* Shared entry checks; consumes a pending forced fault if one is set. */
static hal_status_t precheck(hal_i2c_bus_t *bus, uint8_t addr7)
{
    if (bus == NULL || !bus->in_use || !bus->initialised) {
        return HAL_ERR_NOT_INIT;
    }
    if (addr7 >= I2C_ADDR_SPACE) {
        return HAL_ERR_INVALID_ARG;
    }
    if (bus->fail_next != HAL_OK) {
        hal_status_t forced = bus->fail_next;
        bus->fail_next = HAL_OK;
        return forced;
    }
    if (!bus->present[addr7]) {
        return HAL_ERR_NACK;
    }
    return HAL_OK;
}

hal_status_t hal_i2c_write(hal_i2c_bus_t *bus, uint8_t addr7,
                           const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (data == NULL && len > 0) {
        return HAL_ERR_INVALID_ARG;
    }
    hal_status_t status = precheck(bus, addr7);
    if (status != HAL_OK) {
        return status;
    }
    if (len > 0) {
        uint8_t reg = data[0];
        bus->cursor[addr7] = reg;
        for (size_t i = 1; i < len; ++i) {
            bus->regs[addr7][(uint8_t)(reg + (i - 1))] = data[i];
        }
    }
    return HAL_OK;
}

hal_status_t hal_i2c_read(hal_i2c_bus_t *bus, uint8_t addr7,
                          uint8_t *buffer, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (buffer == NULL && len > 0) {
        return HAL_ERR_INVALID_ARG;
    }
    hal_status_t status = precheck(bus, addr7);
    if (status != HAL_OK) {
        return status;
    }
    uint8_t cur = bus->cursor[addr7];
    for (size_t i = 0; i < len; ++i) {
        buffer[i] = bus->regs[addr7][(uint8_t)(cur + i)];
    }
    bus->cursor[addr7] = (uint8_t)(cur + len);
    return HAL_OK;
}

hal_status_t hal_i2c_write_read(hal_i2c_bus_t *bus, uint8_t addr7,
                                const uint8_t *tx, size_t tx_len,
                                uint8_t *rx, size_t rx_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if ((tx == NULL && tx_len > 0) || (rx == NULL && rx_len > 0)) {
        return HAL_ERR_INVALID_ARG;
    }
    hal_status_t status = precheck(bus, addr7);
    if (status != HAL_OK) {
        return status;
    }
    uint8_t cur = (tx_len > 0) ? tx[0] : bus->cursor[addr7];
    for (size_t i = 0; i < rx_len; ++i) {
        rx[i] = bus->regs[addr7][(uint8_t)(cur + i)];
    }
    bus->cursor[addr7] = (uint8_t)(cur + rx_len);
    return HAL_OK;
}

/* --- test controls --- */

void hal_i2c_sim_set_present(hal_i2c_bus_t *bus, uint8_t addr7, bool present)
{
    if (bus != NULL && addr7 < I2C_ADDR_SPACE) {
        bus->present[addr7] = present;
    }
}

void hal_i2c_sim_set_reg(hal_i2c_bus_t *bus, uint8_t addr7, uint8_t reg, uint8_t value)
{
    if (bus != NULL && addr7 < I2C_ADDR_SPACE) {
        bus->regs[addr7][reg] = value;
    }
}

void hal_i2c_sim_fail_next(hal_i2c_bus_t *bus, hal_status_t status)
{
    if (bus != NULL) {
        bus->fail_next = status;
    }
}
