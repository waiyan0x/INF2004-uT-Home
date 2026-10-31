/**
 * @file senlux.c
 * @brief Nonblocking reviewed SEN0390 enable and light-read sequence.
 */
#include "senlux.h"

static const geni2c_recipe_t g_recipe = {
    .address = 0x4AU, .reg = 0U, .read_size = 4U, .read_pause_ms = 10U};

/** @brief Reserve the address, drive EN low, then wait without sleeping. */
int senlux_open (senlux_t *  p_device,
                 i2cbus_t *  p_bus,
                 digiout_t * p_enable,
                 uint32_t    timeout_ms)
{
    if ((NULL == p_device) || (NULL == p_bus) || (NULL == p_enable) ||
        (NULL == p_enable->p_set) || (NULL == p_enable->p_context) ||
        (NULL != p_device->p_enable))
    {
        return I2CBUS_INVALID;
    }
    int result = geni2c_open(&p_device->sensor, p_bus, &g_recipe, timeout_ms);
    if (0 == result)
    {
        p_device->p_enable = p_enable;
        result             = p_enable->p_set(p_enable->p_context, false);
        if (0 == result)
        {
            uint64_t now = 0U;
            if ((0 != i2cbus_now(p_bus, &now)) || (now > (UINT64_MAX - 1000U)))
            {
                result = I2CBUS_IO;
            }
            else
            {
                p_device->ready_ms  = now + 1000U;
                p_device->b_started = true;
            }
        }
        else
        {
            result = I2CBUS_IO;
        }
    }
    return result;
}

/** @brief Raise EN after one second; permit reads after a further second. */
int senlux_read (senlux_t * p_device, uint32_t * p_millilux)
{
    if ((NULL == p_device) || (NULL == p_millilux) ||
        (NULL == p_device->p_enable) || (!p_device->sensor.b_claimed) ||
        (!p_device->b_started))
    {
        return I2CBUS_INVALID;
    }
    uint64_t now = 0U;
    if (0 != i2cbus_now(p_device->sensor.p_bus, &now))
    {
        return I2CBUS_IO;
    }
    if (now < p_device->ready_ms)
    {
        return UTHOME_SENLUX_WAITING;
    }
    if (!p_device->b_high)
    {
        if (now > (UINT64_MAX - 1000U))
        {
            return I2CBUS_IO;
        }
        const int enabled =
            p_device->p_enable->p_set(p_device->p_enable->p_context, true);
        if (0 != enabled)
        {
            return I2CBUS_IO;
        }
        p_device->b_high    = true;
        p_device->b_started = false;
        if ((0 != i2cbus_now(p_device->sensor.p_bus, &now)) ||
            (now > (UINT64_MAX - 1000U)))
        {
            return I2CBUS_IO;
        }
        p_device->ready_ms  = now + 1000U;
        p_device->b_started = true;
        return UTHOME_SENLUX_WAITING;
    }
    uint8_t data[4] = {0U};
    int     result  = geni2c_read(&p_device->sensor, data, sizeof(data));
    if (0 == result)
    {
        result =
            geni2c_scale_le(data, sizeof(data), 7U, 5U, 200000000U, p_millilux);
    }
    return result;
}

/** @brief Disable first; retain ownership until both cleanup operations
 * succeed. */
int senlux_close (senlux_t * p_device)
{
    if (NULL == p_device)
    {
        return I2CBUS_INVALID;
    }
    if (NULL == p_device->p_enable)
    {
        return 0;
    }
    int result =
        p_device->p_enable->p_set(p_device->p_enable->p_context, false);
    if (0 != result)
    {
        result = I2CBUS_IO;
    }
    if (0 == result)
    {
        result = geni2c_close(&p_device->sensor);
    }
    if (0 == result)
    {
        *p_device = (senlux_t){0};
    }
    return result;
}

/* End of file. */

