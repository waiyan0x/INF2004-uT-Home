#ifndef UTHOME_HAL_HOST_SIM_H
#define UTHOME_HAL_HOST_SIM_H

/*
 * Test-only control surface for the host simulation HAL.
 *
 * Production code (drivers, services) includes only the public contracts in
 * firmware/hal/include/. Unit tests include this header to preload register
 * values, mark devices present or absent, inspect what a driver wrote, and
 * force faults such as the I2C timeout raised when a sensor is disconnected.
 *
 * This header must never be included by target (esp32s3) code.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal_status.h"
#include "i2c.h"
#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clear every simulated bus and device. Call at the start of each test. */
void host_sim_reset(void);

/* --- I2C simulation controls --- */

/* Mark a 7-bit address as present (acknowledges) or absent (returns NACK). */
void hal_i2c_sim_set_present(hal_i2c_bus_t *bus, uint8_t addr7, bool present);

/* Preload one register byte in a simulated device's register file. */
void hal_i2c_sim_set_reg(hal_i2c_bus_t *bus, uint8_t addr7, uint8_t reg, uint8_t value);

/* Force the next transaction on this bus to fail with the given status. */
void hal_i2c_sim_fail_next(hal_i2c_bus_t *bus, hal_status_t status);

/* --- SPI simulation controls --- */

/* Return the bytes captured from transfers to this device since the last clear. */
void hal_spi_sim_get_capture(hal_spi_device_t *device, const uint8_t **out_buf, size_t *out_len);

/* Discard captured bytes for this device. */
void hal_spi_sim_clear_capture(hal_spi_device_t *device);

/* Set the byte returned on the MISO line for full-duplex reads. */
void hal_spi_sim_set_miso(hal_spi_device_t *device, uint8_t pattern);

/* Force the next transfer to this device to fail with the given status. */
void hal_spi_sim_fail_next(hal_spi_device_t *device, hal_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_HAL_HOST_SIM_H */
