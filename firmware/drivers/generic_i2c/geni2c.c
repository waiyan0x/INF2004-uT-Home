/**
 * @file geni2c.c
 * @brief Execute a reviewed register-pointer read with a bounded deadline.
 */
#include "geni2c.h"

#include <string.h>

/** @brief Claim the reviewed recipe's address without accessing hardware. */
int geni2c_open (geni2c_t *              p_sensor,
                 i2cbus_t *              p_bus,
                 const geni2c_recipe_t * p_recipe,
                 uint32_t                timeout_ms)
{
    if ((NULL == p_sensor) || (NULL == p_bus) || (NULL == p_recipe) ||
        (0U == p_recipe->read_size) ||
        (p_recipe->read_size > UTHOME_I2C_RX_MAX) || (0U == timeout_ms) ||
        (p_recipe->read_pause_ms > UTHOME_I2C_PAUSE_MAX_MS) ||
        (timeout_ms <= p_recipe->read_pause_ms) ||
        (timeout_ms > UINT32_C(2147483647)))
    {
        return I2CBUS_INVALID;
    }
    if (p_sensor->b_claimed)
    {
        return I2CBUS_STATE;
    }
    const int result = i2cbus_claim(p_bus, p_recipe->address);
    if (0 == result)
    {
        p_sensor->p_bus      = p_bus;
        p_sensor->recipe     = *p_recipe;
        p_sensor->timeout_ms = timeout_ms;
        p_sensor->b_claimed  = true;
    }
    return result;
}

/** @brief Publish bytes only after a successful complete bus transfer. */
int geni2c_read (geni2c_t * p_sensor, uint8_t * p_output, size_t capacity)
{
    if ((NULL == p_sensor) || (NULL == p_output))
    {
        return I2CBUS_INVALID;
    }
    if ((!p_sensor->b_claimed) || (capacity < p_sensor->recipe.read_size))
    {
        return I2CBUS_STATE;
    }
    uint8_t  buffer[UTHOME_I2C_RX_MAX] = {0U};
    uint64_t deadline                  = 0U;
    int      result =
        i2cbus_deadline(p_sensor->p_bus, p_sensor->timeout_ms, &deadline);
    if (0 == result)
    {
        const i2cbus_request_t request = {
            .address       = p_sensor->recipe.address,
            .p_write       = &p_sensor->recipe.reg,
            .write_size    = 1U,
            .p_read        = buffer,
            .read_size     = p_sensor->recipe.read_size,
            .deadline_ms   = deadline,
            .read_pause_ms = p_sensor->recipe.read_pause_ms};
        result = i2cbus_transfer(p_sensor->p_bus, &request);
    }
    if (0 == result)
    {
        memcpy(p_output, buffer, p_sensor->recipe.read_size);
    }
    return result;
}

/** @brief Release the address only after the worker is stopped. */
int geni2c_close (geni2c_t * p_sensor)
{
    if (NULL == p_sensor)
    {
        return I2CBUS_INVALID;
    }
    if (!p_sensor->b_claimed)
    {
        return 0;
    }
    const int result =
        i2cbus_release(p_sensor->p_bus, p_sensor->recipe.address);
    if (0 == result)
    {
        *p_sensor = (geni2c_t){0};
    }
    return result;
}

/** @brief Scale four little-endian bytes using a reviewed rational ratio. */
int geni2c_scale_le (const uint8_t * p_data,
                     size_t          size,
                     uint32_t        numerator,
                     uint32_t        denominator,
                     uint32_t        maximum,
                     uint32_t *      p_value)
{
    if ((NULL == p_data) || (4U != size) || (0U == denominator) ||
        (NULL == p_value))
    {
        return I2CBUS_INVALID;
    }
    const uint32_t raw = (uint32_t)p_data[0] | ((uint32_t)p_data[1] << 8U) |
                         ((uint32_t)p_data[2] << 16U) |
                         ((uint32_t)p_data[3] << 24U);
    /* Each product fits uint64_t: the operands are at most UINT32_MAX. */
    const uint64_t scaled = ((uint64_t)raw * numerator) / denominator;
    if (scaled > maximum)
    {
        return I2CBUS_INVALID;
    }
    /* The explicit maximum check makes the narrowing conversion safe. */
    *p_value = (uint32_t)scaled;
    return 0;
}

/* End of file. */

