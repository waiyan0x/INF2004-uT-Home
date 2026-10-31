/**
 * @file spiown.c
 * @brief Separate permanent SPI lock creation from repeated bus sessions.
 */
#include "spiown.h"

#include <stddef.h>

static bool valid_operations (const spiown_ops_t * p_operations);
static bool ready (const spiown_t * p_owner);
static int claim_lock (const spiown_t * p_owner);
static int release (const spiown_t * p_owner, int result);

/** @brief Create the permanent lock once; repeated calls create nothing. */
int spiown_init (spiown_t *           p_owner,
                 const spiown_ops_t * p_operations,
                 void *               p_context)
{
    if (NULL == p_owner)
    {
        return SPIBUS_INVALID;
    }
    if (SPIOWN_UNINIT != p_owner->status)
    {
        /* A failed creation is final: the bus stays unavailable until reboot.
         */
        return (SPIOWN_READY == p_owner->status) ? 0 : SPIBUS_STATE;
    }
    if ((!valid_operations(p_operations)) || (NULL == p_context))
    {
        return SPIBUS_INVALID;
    }
    p_owner->p_operations = p_operations;
    p_owner->p_context    = p_context;
    p_owner->phase        = SPIOWN_CLOSED;
    if (0 != p_operations->p_create(p_context))
    {
        p_owner->status = SPIOWN_FAILED;
        return SPIBUS_IO;
    }
    p_owner->status = SPIOWN_READY;
    return 0;
}

/** @brief Start one session under the permanent lock without creating it. */
int spiown_open (spiown_t *       p_owner,
                 const spibus_t * p_bus,
                 const void *     p_settings)
{
    if ((NULL == p_owner) || (NULL == p_bus) || (NULL == p_settings))
    {
        return SPIBUS_INVALID;
    }
    /* Only the lifecycle owner writes the phase, so it may read it unlocked.
     * A device recorded while closed belongs to a misbehaving caller; refuse
     * rather than silently discard its registration.
     */
    if ((!ready(p_owner)) || (SPIOWN_CLOSED != p_owner->phase) ||
        (!p_owner->p_operations->p_owner(p_owner->p_context)) ||
        (spibus_has_devices(p_bus)))
    {
        return SPIBUS_STATE;
    }
    const int locked = claim_lock(p_owner);
    if (0 != locked)
    {
        return locked;
    }
    const int started =
        p_owner->p_operations->p_start(p_owner->p_context, p_settings);
    if (0 == started)
    {
        p_owner->phase = SPIOWN_OPEN;
    }
    return release(p_owner, started);
}

/** @brief End a quiescent session; failed cleanup stays retained for retry. */
int spiown_close (spiown_t * p_owner, const spibus_t * p_bus)
{
    if ((NULL == p_owner) || (NULL == p_bus))
    {
        return SPIBUS_INVALID;
    }
    if ((!ready(p_owner)) || (SPIOWN_CLOSED == p_owner->phase) ||
        (!p_owner->p_operations->p_owner(p_owner->p_context)) ||
        (spibus_has_devices(p_bus)))
    {
        return SPIBUS_STATE;
    }
    const int locked = claim_lock(p_owner);
    if (0 != locked)
    {
        return locked;
    }
    /* Block transfers before touching hardware, whatever stop reports. */
    p_owner->phase    = SPIOWN_RETAINED;
    const int stopped = p_owner->p_operations->p_stop(p_owner->p_context);
    if (0 == stopped)
    {
        p_owner->phase = SPIOWN_CLOSED;
    }
    return release(p_owner, stopped);
}

/** @brief Hold the lock for one transfer only while the session is open. */
int spiown_begin (spiown_t * p_owner, uint32_t timeout_ms)
{
    /* Status changes only during startup, before any transfer can run. */
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return SPIBUS_STATE;
    }
    const int locked =
        p_owner->p_operations->p_lock(p_owner->p_context, timeout_ms);
    if (0 != locked)
    {
        return locked;
    }
    return (SPIOWN_OPEN == p_owner->phase) ? 0 : release(p_owner, SPIBUS_STATE);
}

/** @brief Release the lock acquired by a successful spiown_begin. */
int spiown_end (spiown_t * p_owner)
{
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return SPIBUS_STATE;
    }
    return release(p_owner, 0);
}

/** @brief Report the session phase without the lifecycle-owner check. */
bool spiown_is_open (const spiown_t * p_owner)
{
    return (NULL != p_owner) && (ready(p_owner)) &&
           (SPIOWN_OPEN == p_owner->phase);
}

/** @brief Require every backend callback before creating anything. */
static bool valid_operations (const spiown_ops_t * p_operations)
{
    return (NULL != p_operations) && (NULL != p_operations->p_create) &&
           (NULL != p_operations->p_owner) && (NULL != p_operations->p_lock) &&
           (NULL != p_operations->p_unlock) &&
           (NULL != p_operations->p_start) && (NULL != p_operations->p_stop);
}

/** @brief Accept only a successfully created permanent lock. */
static bool ready (const spiown_t * p_owner)
{
    return SPIOWN_READY == p_owner->status;
}

/** @brief Poll once; a transfer holding the lock means the bus is in use. */
static int claim_lock (const spiown_t * p_owner)
{
    const int locked = p_owner->p_operations->p_lock(p_owner->p_context, 0U);
    return (SPIBUS_TIMEOUT == locked) ? SPIBUS_STATE : locked;
}

/** @brief Unlock, reporting an unlock failure over the operation's result. */
static int release (const spiown_t * p_owner, int result)
{
    if (0 != p_owner->p_operations->p_unlock(p_owner->p_context))
    {
        return SPIBUS_IO;
    }
    return result;
}

/* End of file. */

