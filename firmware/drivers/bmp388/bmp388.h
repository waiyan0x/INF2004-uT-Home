#ifndef UTHOME_DRIVER_BMP388_H
#define UTHOME_DRIVER_BMP388_H

#include <stdint.h>

#include "hal_status.h"
#include "i2c.h"
#include "uthome/sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BMP388 pressure and temperature driver.
 *
 * A thin adapter over the Bosch BMP3_SensorAPI (firmware/third_party/bmp3).
 * The vendor code owns register access and compensation; this driver wires it
 * onto the I2C HAL, verifies the chip on init, and returns an
 * environment_sample. It creates no tasks, queues, or pins: the runtime calls
 * bmp388_read() on its own timer and the profile supplies these fields.
 */

typedef struct {
    hal_i2c_bus_t *bus;         /* Initialised I2C bus the sensor sits on */
    uint8_t        addr7;       /* 7-bit address: 0x76 (prototype) or 0x77 */
    uint32_t       timeout_ms;  /* Per-transaction timeout */

    /* Optional microsecond delay, supplied by the runtime/time HAL. The Bosch
     * driver needs a short delay during reset and mode changes. NULL selects a
     * no-op, which is correct for host tests but must be wired to a real delay
     * on the esp32s3 port before hardware bring-up. */
    void (*delay_us)(uint32_t us);
} bmp388_config_t;

/* Opaque instance handle, backed by static storage in the driver. */
typedef struct bmp388 bmp388_t;

/*
 * Verify the chip id, soft-reset, load calibration, and apply a periodic
 * pressure+temperature sampling configuration. On success returns HAL_OK and a
 * handle in *out_dev; on failure returns an error and sets *out_dev to NULL:
 * HAL_ERR_INVALID_ARG for an address other than 0x76/0x77 or a sensor already
 * initialised at that bus and address, HAL_ERR_UNSUPPORTED
 * if the chip answers with the wrong id, or the I2C HAL status (for example
 * HAL_ERR_NACK when nothing is at the address) if the bus fails.
 */
hal_status_t bmp388_init(const bmp388_config_t *config, bmp388_t **out_dev);

/*
 * Read one compensated sample. The returned status is also copied into
 * out_sample->status so a stale or failed reading is visible downstream. A bus
 * failure returns the I2C HAL status, e.g. HAL_ERR_TIMEOUT when the sensor is
 * disconnected. timestamp_ms is left zero for the runtime to fill.
 */
hal_status_t bmp388_read(bmp388_t *dev, environment_sample_t *out_sample);

/* Release the instance. */
hal_status_t bmp388_deinit(bmp388_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_DRIVER_BMP388_H */
