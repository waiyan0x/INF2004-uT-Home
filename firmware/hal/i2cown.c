/**
 * @file i2cown.c
 * @brief Separate permanent lock creation from repeated bus sessions.
 */
#include "i2cown.h"

#include <stddef.h>

static bool valid_operations (const i2cown_ops_t * p_operations);
static bool ready (const i2cown_t * p_owner);
static int claim_lock (const i2cown_t * p_owner);
static int release (const i2cown_t * p_owner, int result);

/** @brief Create the permanent lock once; repeated calls create nothing. */
int i2cown_init (i2cown_t *           p_owner,
                 const i2cown_ops_t * p_operations,
                 void *               p_context)
{
    if (NULL == p_owner)
    {
        return I2CBUS_INVALID;
    }
    if (I2COWN_UNINIT != p_owner->status)
    {
        /* A failed creation is final: the bus stays unavailable until reboot.
         */
        return (I2COWN_READY == p_owner->status) ? 0 : I2CBUS_STATE;
    }
    if ((!valid_operations(p_operations)) || (NULL == p_context))
    {
        return I2CBUS_INVALID;
    }
    p_owner->p_operations = p_operations;
    p_owner->p_context    = p_context;
    p_owner->phase        = I2COWN_CLOSED;
    if (0 != p_operations->p_create(p_context))
    {
        p_owner->status = I2COWN_FAILED;
        return I2CBUS_IO;
    }
    p_owner->status = I2COWN_READY;
    return 0;
}

/** @brief Start one session under the permanent lock without creating it. */
int i2cown_open (i2cown_t * p_owner, const i2cbus_config_t * p_config)
{
    if ((NULL == p_owner) || (NULL == p_config))
    {
        return I2CBUS_INVALID;
    }
    /* Only the lifecycle owner writes the phase, so it may read it unlocked.
     */
    if ((!ready(p_owner)) || (I2COWN_CLOSED != p_owner->phase) ||
        (!p_owner->p_operations->p_owner(p_owner->p_context)))
    {
        return I2CBUS_STATE;
    }
    const int locked = claim_lock(p_owner);
    if (0 != locked)
    {
        return locked;
    }
    const int started =
        p_owner->p_operations->p_start(p_owner->p_context, p_config);
    if (0 == started)
    {
        p_owner->phase = I2COWN_OPEN;
    }
    return release(p_owner, started);
}

/** @brief End a quiescent session; failed cleanup stays retained for retry. */
int i2cown_close (i2cown_t * p_owner, const i2cbus_t * p_bus)
{
    if ((NULL == p_owner) || (NULL == p_bus))
    {
        return I2CBUS_INVALID;
    }
    if ((!ready(p_owner)) || (I2COWN_CLOSED == p_owner->phase) ||
        (!p_owner->p_operations->p_owner(p_owner->p_context)) ||
        (i2cbus_has_claims(p_bus)))
    {
        return I2CBUS_STATE;
    }
    const int locked = claim_lock(p_owner);
    if (0 != locked)
    {
        return locked;
    }
    /* Block transfers before touching hardware, whatever stop reports. */
    p_owner->phase    = I2COWN_RETAINED;
    const int stopped = p_owner->p_operations->p_stop(p_owner->p_context);
    if (0 == stopped)
    {
        p_owner->phase = I2COWN_CLOSED;
    }
    return release(p_owner, stopped);
}

/** @brief Hold the lock for one transfer only while the session is open. */
int i2cown_begin (i2cown_t * p_owner, uint32_t timeout_ms)
{
    /* Status changes only during startup, before any transfer can run. */
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return I2CBUS_STATE;
    }
    const int locked =
        p_owner->p_operations->p_lock(p_owner->p_context, timeout_ms);
    if (0 != locked)
    {
        return locked;
    }
    return (I2COWN_OPEN == p_owner->phase) ? 0 : release(p_owner, I2CBUS_STATE);
}

/** @brief Release the lock acquired by a successful i2cown_begin. */
int i2cown_end (i2cown_t * p_owner)
{
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return I2CBUS_STATE;
    }
    return release(p_owner, 0);
}

/** @brief Give clock and delay callbacks a cheap session hint. */
bool i2cown_is_open (const i2cown_t * p_owner)
{
    return (NULL != p_owner) && (ready(p_owner)) &&
           (I2COWN_OPEN == p_owner->phase);
}

/** @brief Require every backend callback before creating anything. */
static bool valid_operations (const i2cown_ops_t * p_operations)
{
    return (NULL != p_operations) && (NULL != p_operations->p_create) &&
           (NULL != p_operations->p_owner) && (NULL != p_operations->p_lock) &&
           (NULL != p_operations->p_unlock) &&
           (NULL != p_operations->p_start) && (NULL != p_operations->p_stop);
}

/** @brief Accept only a successfully created permanent lock. */
static bool ready (const i2cown_t * p_owner)
{
    return I2COWN_READY == p_owner->status;
}

/** @brief Poll once; a transfer holding the lock means the bus is in use. */
static int claim_lock (const i2cown_t * p_owner)
{
    const int locked = p_owner->p_operations->p_lock(p_owner->p_context, 0U);
    return (I2CBUS_TIMEOUT == locked) ? I2CBUS_STATE : locked;
}

/** @brief Unlock, reporting an unlock failure over the operation's result. */
static int release (const i2cown_t * p_owner, int result)
{
    if (0 != p_owner->p_operations->p_unlock(p_owner->p_context))
    {
        return I2CBUS_IO;
    }
    return result;
}

/* End of file. */

