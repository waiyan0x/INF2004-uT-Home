/**
 * @file txi2c.h
 * @brief Serialized experiment adapter using the existing generic I2C driver.
 */
#ifndef UTHOME_TXI2C_H
#define UTHOME_TXI2C_H
#include "txrule.h"
#include "geni2c.h"

typedef struct
{
    i2cbus_t * p_bus;
    geni2c_t   sensor;
} txi2c_t;

/* Caller initializes storage with the already-open bus, owns all bus claims,
 * and serializes calls with configuration and workers. No parallel claimant. */
txrule_status_t
txi2c_read (void * p_context, const txrule_config_t * p_rule, uint8_t * p_data);
#endif

/* End of file. */

