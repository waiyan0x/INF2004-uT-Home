/**
 * @file i2cown.h
 * @brief Keep one permanent I2C bus lock while bus sessions open and close.
 */
#ifndef UTHOME_I2COWN_H
#define UTHOME_I2COWN_H

#include <stdbool.h>
#include <stdint.h>
#include "i2cbus.h"
#include "buslife.h"

/* Shared lifecycle storage preserves the permanent state footprint. */
typedef buslife_status_t i2cown_status_t;
typedef buslife_phase_t  i2cown_phase_t;
typedef buslife_create_t i2cown_create_t;
typedef buslife_owner_t  i2cown_owner_t;
typedef buslife_lock_t   i2cown_lock_t;
typedef buslife_unlock_t i2cown_unlock_t;
typedef buslife_start_t  i2cown_start_t;
typedef buslife_stop_t   i2cown_stop_t;
typedef buslife_ops_t    i2cown_ops_t;
typedef buslife_t        i2cown_t;

#define I2COWN_UNINIT   BUSLIFE_UNINIT
#define I2COWN_READY    BUSLIFE_READY
#define I2COWN_FAILED   BUSLIFE_FAILED
#define I2COWN_CLOSED   BUSLIFE_CLOSED
#define I2COWN_OPEN     BUSLIFE_OPEN
#define I2COWN_RETAINED BUSLIFE_RETAINED

/* p_start receives the settings supplied through the typed wrapper below.
 * Operations and context remain alive for the program lifetime.
 */
/* Call once during startup, before any open or transfer. Later calls never
 * create another lock: they return 0 after success or I2CBUS_STATE after a
 * failed initialization, which leaves the bus unavailable until reboot.
 */
int i2cown_init (i2cown_t *           p_owner,
                 const i2cown_ops_t * p_operations,
                 void *               p_context);
/* Lifecycle owner only. Requires READY and CLOSED; polls the lock. */
int i2cown_open (i2cown_t * p_owner, const i2cbus_config_t * p_config);
/* Lifecycle owner only. Rejects device claims and a transfer holding the lock.
 * A stop failure leaves the session RETAINED for an explicit retry.
 */
int i2cown_close (i2cown_t * p_owner, const i2cbus_t * p_bus);
/* Lock for one transfer, then require an OPEN session; pair with i2cown_end.
 */
int i2cown_begin (i2cown_t * p_owner, uint32_t timeout_ms);
int i2cown_end (i2cown_t * p_owner);
/* Unlocked hint for clock/delay callbacks; i2cown_begin is authoritative. */
bool i2cown_is_open (const i2cown_t * p_owner);

#endif

/* End of file. */

