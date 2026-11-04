/**
 * @file geni2c.h
 * @brief Fixed-storage register reads for reviewed I2C recipes.
 */
#ifndef UTHOME_GENI2C_H
#define UTHOME_GENI2C_H

#include "i2cbus.h"

/* Recipes must be immutable, reviewed build inputs, not model output or profile
 * data. This primitive does not identify a device, control EN or decode values.
 */
typedef struct
{
    uint8_t address;
    uint8_t reg;
    uint8_t read_size;
    uint8_t read_pause_ms;
} geni2c_recipe_t;

/* Zero storage before the first open; serialize calls for each context.
 * Read outputs must not overlap the context, bus or recipe storage. */
typedef struct
{
    i2cbus_t *      p_bus;
    geni2c_recipe_t recipe;
    uint32_t        timeout_ms;
    bool            b_claimed;
} geni2c_t;

int geni2c_open (geni2c_t *              p_sensor,
                 i2cbus_t *              p_bus,
                 const geni2c_recipe_t * p_recipe,
                 uint32_t                timeout_ms);
int geni2c_read (geni2c_t * p_sensor, uint8_t * p_output, size_t capacity);
int geni2c_close (geni2c_t * p_sensor);
int geni2c_scale_le (const uint8_t * p_data,
                     size_t          size,
                     uint32_t        numerator,
                     uint32_t        denominator,
                     uint32_t        maximum,
                     uint32_t *      p_value);

#endif

/* End of file. */

