/**
 * @file txi2c.c
 * @brief Reuse the register-read driver without bypassing address ownership.
 */
#include "txi2c.h"

/** @brief Claim, read with a 50 ms deadline, and release under one owner. */
txrule_status_t
txi2c_read (void * p_context, const txrule_config_t * p_rule, uint8_t * p_data)
{
    txi2c_t * p_adapter = p_context;
    if ((NULL == p_adapter) || (NULL == p_rule) || (NULL == p_data) ||
        (NULL == p_adapter->p_bus) || (p_adapter->sensor.b_claimed))
    {
        return TXRULE_IO;
    }
    const geni2c_recipe_t recipe = {.address       = (uint8_t)p_rule->address,
                                    .reg           = (uint8_t)p_rule->reg,
                                    .read_size     = (uint8_t)p_rule->read_size,
                                    .read_pause_ms = 0U};
    /* Only admitted txrule configurations reach this internal adapter. */
    int status =
        geni2c_open(&p_adapter->sensor, p_adapter->p_bus, &recipe, 50U);
    if (I2CBUS_OK == status)
    {
        status = geni2c_read(&p_adapter->sensor, p_data, TXRULE_DATA_SIZE);
        const int closed = geni2c_close(&p_adapter->sensor);
        if (I2CBUS_OK != closed)
        {
            return TXRULE_IO;
        }
    }
    return (I2CBUS_OK == status)
               ? TXRULE_OK
               : ((I2CBUS_TIMEOUT == status) ? TXRULE_TIMEOUT : TXRULE_IO);
}

/* End of file. */

