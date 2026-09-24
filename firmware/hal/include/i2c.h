#ifndef UTHOME_HAL_I2C_H
#define UTHOME_HAL_I2C_H

#include <stddef.h>
#include <stdint.h>
#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C bus abstraction.
 *
 * One bus is shared by several devices; each transaction names its 7-bit
 * device address. This mirrors the "i2c" interface object in a node profile
 * (id, port, sda_gpio, scl_gpio, clock_hz). Pin numbers come from the
 * validated profile and are never compiled into a driver.
 *
 * Concurrency: devices on one bus may be driven from different tasks (e.g. the
 * BMP388 and a light sensor both on i2c0). The port serialises transactions
 * with a per-bus lock, so each write, read or write-read is atomic on the
 * wire. init and deinit are not thread-safe; the runtime calls them from one
 * task during start-up or reconfiguration. The host_sim port is
 * single-threaded and does not lock.
 */

typedef struct {
    uint8_t  port;      /* Controller index, e.g. 0 for "i2c0" */
    uint8_t  sda_gpio;  /* Data line GPIO */
    uint8_t  scl_gpio;  /* Clock line GPIO */
    uint32_t clock_hz;  /* Bus clock, e.g. 400000 */
} hal_i2c_config_t;

/* Opaque bus handle. The port backs it with static storage; no heap use. */
typedef struct hal_i2c_bus hal_i2c_bus_t;

/*
 * Bring up an I2C bus from a validated configuration.
 * On success returns HAL_OK and a usable handle in *out_bus; on failure
 * returns an error and sets *out_bus to NULL. HAL_ERR_INVALID_ARG covers a
 * zero clock_hz and a port that is already initialised.
 */
hal_status_t hal_i2c_init(const hal_i2c_config_t *config, hal_i2c_bus_t **out_bus);

/* Release the bus. Valid to call once after a successful init. */
hal_status_t hal_i2c_deinit(hal_i2c_bus_t *bus);

/*
 * Write len bytes to the device at addr7.
 * Returns HAL_ERR_NACK if the device does not acknowledge, HAL_ERR_TIMEOUT
 * if the transfer does not finish within timeout_ms.
 */
hal_status_t hal_i2c_write(hal_i2c_bus_t *bus, uint8_t addr7,
                           const uint8_t *data, size_t len, uint32_t timeout_ms);

/* Read len bytes from the device at addr7. */
hal_status_t hal_i2c_read(hal_i2c_bus_t *bus, uint8_t addr7,
                          uint8_t *buffer, size_t len, uint32_t timeout_ms);

/*
 * Combined write-then-read using a repeated start: the usual register-read
 * pattern (send the register index, then read its value without releasing the
 * bus). This is the call a BMP388 driver uses for chip-id and data reads.
 */
hal_status_t hal_i2c_write_read(hal_i2c_bus_t *bus, uint8_t addr7,
                                const uint8_t *tx, size_t tx_len,
                                uint8_t *rx, size_t rx_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_HAL_I2C_H */
