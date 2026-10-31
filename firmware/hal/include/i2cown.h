/**
 * @file i2cown.h
 * @brief Keep one permanent I2C bus lock while bus sessions open and close.
 */
#ifndef UTHOME_I2COWN_H
#define UTHOME_I2COWN_H

#include <stdbool.h>
#include <stdint.h>
#include "i2cbus.h"

/* Permanent lifecycle. Only i2cown_init leaves UNINIT, exactly once. */
typedef enum
{
    I2COWN_UNINIT = 0,
    I2COWN_READY  = 1,
    I2COWN_FAILED = 2
} i2cown_status_t;

/* Session phase. RETAINED means cleanup failed: retry close, never reuse. */
typedef enum
{
    I2COWN_CLOSED   = 0,
    I2COWN_OPEN     = 1,
    I2COWN_RETAINED = 2
} i2cown_phase_t;

/* Create the permanent lock; called at most once, during i2cown_init. */
typedef int i2cown_create_t (void * p_context);
/* Report whether the caller is the serialized lifecycle owner. */
typedef bool i2cown_owner_t (void * p_context);
/* Lock within timeout_ms; zero polls. Return 0, I2CBUS_TIMEOUT or a failure.
 */
typedef int i2cown_lock_t (void * p_context, uint32_t timeout_ms);
typedef int i2cown_unlock_t (void * p_context);
/* Acquire pins and start the controller. Failure must leave neither held. */
typedef int i2cown_start_t (void * p_context, const i2cbus_config_t * p_config);
/* Stop the controller and release pins. Failure keeps pin ownership. */
typedef int i2cown_stop_t (void * p_context);

typedef struct
{
    i2cown_create_t * p_create;
    i2cown_owner_t *  p_owner;
    i2cown_lock_t *   p_lock;
    i2cown_unlock_t * p_unlock;
    i2cown_start_t *  p_start;
    i2cown_stop_t *   p_stop;
} i2cown_ops_t;

/* Zero-initialize permanent storage. Phase changes happen with the lock held.
 * Operations and context remain alive for the life of the program.
 */
typedef struct
{
    const i2cown_ops_t * p_operations;
    void *               p_context;
    i2cown_status_t      status;
    i2cown_phase_t       phase;
} i2cown_t;

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

