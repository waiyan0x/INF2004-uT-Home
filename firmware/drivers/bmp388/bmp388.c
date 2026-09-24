/*
 * BMP388 driver: adapter between the I2C HAL (firmware/hal/include/i2c.h) and
 * the Bosch BMP3_SensorAPI (firmware/third_party/bmp3). All register access
 * and compensation is Bosch's; this file only supplies the three callbacks the
 * vendor driver needs, applies a sampling configuration, and maps results into
 * an environment_sample.
 */

#include "bmp388.h"

#include <stdbool.h>
#include <string.h>

#include "bmp3.h"

#define BMP388_MAX_INSTANCES 2

/* Context handed to the Bosch driver through intf_ptr. */
typedef struct {
    hal_i2c_bus_t *bus;
    uint8_t        addr7;
    uint32_t       timeout_ms;
    void         (*user_delay_us)(uint32_t us);
    hal_status_t   bus_error;  /* First HAL failure in the current operation */
} bmp388_intf_t;

struct bmp388 {
    bool                 in_use;
    bmp388_intf_t        intf;
    struct bmp3_dev      dev;
    struct bmp3_settings settings;
};

static struct bmp388 g_instances[BMP388_MAX_INSTANCES];

/* --- HAL adapter callbacks required by the Bosch driver --- */

/*
 * Keep the first HAL failure so the caller sees the real cause (timeout, NACK)
 * rather than the Bosch driver's generic BMP3_E_COMM_FAIL.
 */
static BMP3_INTF_RET_TYPE record_status(bmp388_intf_t *ctx, hal_status_t status)
{
    if (status != HAL_OK && ctx->bus_error == HAL_OK) {
        ctx->bus_error = status;
    }
    return (status == HAL_OK) ? BMP3_INTF_RET_SUCCESS : (BMP3_INTF_RET_TYPE)-1;
}

static BMP3_INTF_RET_TYPE bmp388_i2c_read(uint8_t reg_addr, uint8_t *read_data,
                                          uint32_t len, void *intf_ptr)
{
    bmp388_intf_t *ctx = (bmp388_intf_t *)intf_ptr;
    hal_status_t status = hal_i2c_write_read(ctx->bus, ctx->addr7,
                                             &reg_addr, 1,
                                             read_data, (size_t)len,
                                             ctx->timeout_ms);
    return record_status(ctx, status);
}

/*
 * For a multi-register write the Bosch driver passes the data interleaved with
 * the remaining register addresses: registers [reg_addr, r1, r2, ...] arrive as
 * write_data = [d0, r1, d1, r2, d2, ...]. v2.0.6 also reports len as twice the
 * register count while filling one byte less, so the last byte is
 * uninitialised. Each (register, data) pair is sent as its own two-byte write,
 * which any I2C register device accepts and which never sends that last byte.
 */
static BMP3_INTF_RET_TYPE bmp388_i2c_write(uint8_t reg_addr, const uint8_t *write_data,
                                           uint32_t len, void *intf_ptr)
{
    bmp388_intf_t *ctx = (bmp388_intf_t *)intf_ptr;
    uint8_t pair[2];

    if (len == 0) {
        return BMP3_INTF_RET_SUCCESS;
    }

    pair[0] = reg_addr;
    pair[1] = write_data[0];
    hal_status_t status = hal_i2c_write(ctx->bus, ctx->addr7, pair, sizeof(pair),
                                        ctx->timeout_ms);

    for (uint32_t i = 1; status == HAL_OK && i + 1 < len; i += 2) {
        pair[0] = write_data[i];
        pair[1] = write_data[i + 1];
        status = hal_i2c_write(ctx->bus, ctx->addr7, pair, sizeof(pair),
                               ctx->timeout_ms);
    }
    return record_status(ctx, status);
}

static void bmp388_delay_us(uint32_t period, void *intf_ptr)
{
    bmp388_intf_t *ctx = (bmp388_intf_t *)intf_ptr;
    if (ctx != NULL && ctx->user_delay_us != NULL) {
        ctx->user_delay_us(period);
    }
}

/*
 * Map a Bosch result code onto the common HAL status. A communication failure
 * reports the HAL error recorded by the callbacks.
 */
static hal_status_t map_bmp3_status(const bmp388_intf_t *ctx, int8_t rslt)
{
    switch (rslt) {
    case BMP3_OK:              return HAL_OK;
    case BMP3_E_NULL_PTR:      return HAL_ERR_INVALID_ARG;
    case BMP3_E_DEV_NOT_FOUND: return HAL_ERR_UNSUPPORTED; /* Acked, but not a BMP388/BMP390 */
    case BMP3_E_COMM_FAIL:
        return (ctx->bus_error != HAL_OK) ? ctx->bus_error : HAL_ERR_BUS;
    default:                   return HAL_ERR_BUS;
    }
}

/* --- public API --- */

hal_status_t bmp388_init(const bmp388_config_t *config, bmp388_t **out_dev)
{
    if (config == NULL || out_dev == NULL || config->bus == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    *out_dev = NULL;
    if (config->addr7 != BMP3_ADDR_I2C_PRIM && config->addr7 != BMP3_ADDR_I2C_SEC) {
        return HAL_ERR_INVALID_ARG;
    }

    struct bmp388 *inst = NULL;
    for (int i = 0; i < BMP388_MAX_INSTANCES; ++i) {
        if (g_instances[i].in_use) {
            if (g_instances[i].intf.bus == config->bus &&
                g_instances[i].intf.addr7 == config->addr7) {
                return HAL_ERR_INVALID_ARG; /* Sensor already initialised */
            }
        } else if (inst == NULL) {
            inst = &g_instances[i];
        }
    }
    if (inst == NULL) {
        return HAL_ERR_NO_RESOURCE;
    }

    memset(inst, 0, sizeof(*inst));
    inst->intf.bus = config->bus;
    inst->intf.addr7 = config->addr7;
    inst->intf.timeout_ms = config->timeout_ms;
    inst->intf.user_delay_us = config->delay_us;

    inst->dev.intf = BMP3_I2C_INTF;
    inst->dev.intf_ptr = &inst->intf;
    inst->dev.read = bmp388_i2c_read;
    inst->dev.write = bmp388_i2c_write;
    inst->dev.delay_us = bmp388_delay_us;

    int8_t rslt = bmp3_init(&inst->dev);
    if (rslt != BMP3_OK) {
        return map_bmp3_status(&inst->intf, rslt);
    }

    /* Periodic pressure + temperature sampling with modest oversampling. */
    inst->settings.press_en = BMP3_ENABLE;
    inst->settings.temp_en = BMP3_ENABLE;
    inst->settings.odr_filter.press_os = BMP3_OVERSAMPLING_4X;
    inst->settings.odr_filter.temp_os = BMP3_OVERSAMPLING_2X;
    inst->settings.odr_filter.iir_filter = BMP3_IIR_FILTER_COEFF_3;
    inst->settings.odr_filter.odr = BMP3_ODR_25_HZ;

    uint32_t settings_sel = BMP3_SEL_PRESS_EN | BMP3_SEL_TEMP_EN |
                            BMP3_SEL_PRESS_OS | BMP3_SEL_TEMP_OS |
                            BMP3_SEL_IIR_FILTER | BMP3_SEL_ODR;
    rslt = bmp3_set_sensor_settings(settings_sel, &inst->settings, &inst->dev);
    if (rslt != BMP3_OK) {
        return map_bmp3_status(&inst->intf, rslt);
    }

    inst->settings.op_mode = BMP3_MODE_NORMAL;
    rslt = bmp3_set_op_mode(&inst->settings, &inst->dev);
    if (rslt != BMP3_OK) {
        return map_bmp3_status(&inst->intf, rslt);
    }

    inst->in_use = true;
    *out_dev = inst;
    return HAL_OK;
}

hal_status_t bmp388_read(bmp388_t *dev, environment_sample_t *out_sample)
{
    if (dev == NULL || out_sample == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    if (!dev->in_use) {
        return HAL_ERR_NOT_INIT;
    }

    struct bmp3_data data;
    memset(&data, 0, sizeof(data));

    dev->intf.bus_error = HAL_OK;
    int8_t rslt = bmp3_get_sensor_data(BMP3_PRESS | BMP3_TEMP, &data, &dev->dev);
    hal_status_t status = map_bmp3_status(&dev->intf, rslt);

    out_sample->pressure_pa = (float)data.pressure;
    out_sample->temperature_c = (float)data.temperature;
    out_sample->timestamp_ms = 0; /* runtime stamps this on publish */
    out_sample->status = status;
    return status;
}

hal_status_t bmp388_deinit(bmp388_t *dev)
{
    if (dev == NULL || !dev->in_use) {
        return HAL_ERR_NOT_INIT;
    }
    memset(dev, 0, sizeof(*dev));
    return HAL_OK;
}
