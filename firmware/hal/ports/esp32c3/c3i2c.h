/**
 * @file c3i2c.h
 * @brief Own the ESP32-C3 I2C controller behind the portable bus interface.
 */
#ifndef UTHOME_C3I2C_H
#define UTHOME_C3I2C_H

#include "i2cbus.h"

/* One lifecycle owner opens/closes the bus with no peripheral workers owned.
 * Device contexts share the returned bus; close rejects outstanding claims.
 * External pull-ups and validated electrical wiring are required.
 * This port is exclusive: do not install another SDK/native I2C driver.
 */
int c3i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus);
int c3i2c_close (i2cbus_t * p_bus);

#endif

/* End of file. */

