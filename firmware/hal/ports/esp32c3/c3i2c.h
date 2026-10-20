/**
 * @file c3i2c.h
 * @brief Own the ESP32-C3 I2C controller behind the portable bus interface.
 */
#ifndef UTHOME_C3I2C_H
#define UTHOME_C3I2C_H

#include "i2cbus.h"

/* Call once during startup, before profile restore or any request. It creates
 * the only kernel object, one permanent mutex. Later calls create nothing;
 * after a failed creation, init/open keep failing until reboot.
 */
int c3i2c_init (void);
/* The lifecycle owner opens/closes sessions; neither creates nor deletes a
 * kernel object. Device contexts share the returned bus. Close rejects device
 * claims and an in-flight transfer; a failed pin release retains the session,
 * rejecting open and transfers until close is retried successfully.
 * External pull-ups and validated electrical wiring are required.
 * This port is exclusive: do not install another SDK/native I2C driver.
 */
int c3i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus);
int c3i2c_close (i2cbus_t * p_bus);

#endif

/* End of file. */

