/**
 * @file i2cfake.h
 * @brief Deterministic two-address register bus for driver contract tests.
 */
#ifndef UTHOME_I2CFAKE_H
#define UTHOME_I2CFAKE_H

#include "i2cbus.h"

typedef struct
{
    i2cbus_t bus;
    uint8_t  registers[2][256];
    uint8_t  position[2];
    bool     b_present[2];
    uint64_t now_ms;
    uint32_t transfer_ms;
    uint32_t transfers;
    uint32_t writes;
    uint32_t delays;
    uint32_t fail_at;
    int      error;
} i2cfake_t;

/* Init requires zero live claims/contexts. The fake is single-threaded and
 * models register transport only, not electrical timing or physical accuracy.
 * Its addresses 0x76/0x77 are fixtures, not limits on the portable I2C API.
 */
void i2cfake_init (i2cfake_t * p_fake);
void i2cfake_sensor (i2cfake_t * p_fake, size_t index, uint16_t pressure_base);

#endif

/* End of file. */

