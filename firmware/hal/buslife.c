/**
 * @file buslife.c
 * @brief Share permanent lock creation and bounded bus-session transitions.
 */
#include "buslife.h"

#include <stddef.h>

static bool valid_operations (const buslife_ops_t * p_operations);
static bool ready (const buslife_t * p_owner);
static int claim_lock (const buslife_t *        p_owner,
                       const buslife_errors_t * p_errors);
static int release (const buslife_t *        p_owner,
                    int                      result,
                    const buslife_errors_t * p_errors);

/** @brief Create the permanent lock once; repeated calls create nothing. */
int buslife_init (buslife_t *              p_owner,
                  const buslife_ops_t *    p_operations,
                  void *                   p_context,
                  const buslife_errors_t * p_errors)
{
    if (NULL == p_owner)
    {
        return p_errors->invalid;
    }
    if (BUSLIFE_UNINIT != p_owner->status)
    {
        /* A failed creation is final: the bus stays unavailable until reboot.
         */
        return (BUSLIFE_READY == p_owner->status) ? 0 : p_errors->state;
    }
    if ((!valid_operations(p_operations)) || (NULL == p_context))
    {
        return p_errors->invalid;
    }
    p_owner->p_operations = p_operations;
    p_owner->p_context    = p_context;
    p_owner->phase        = BUSLIFE_CLOSED;
    if (0 != p_operations->p_create(p_context))
    {
        p_owner->status = BUSLIFE_FAILED;
        return p_errors->io_error;
    }
    p_owner->status = BUSLIFE_READY;
    return 0;
}

/** @brief Check the lifecycle owner before a wrapper reads device claims. */
bool buslife_can_open (const buslife_t * p_owner)
{
    return (NULL != p_owner) && (ready(p_owner)) &&
           (BUSLIFE_CLOSED == p_owner->phase) &&
           (p_owner->p_operations->p_owner(p_owner->p_context));
}

/** @brief Allow retained cleanup retry by the serialized lifecycle owner. */
bool buslife_can_close (const buslife_t * p_owner)
{
    return (NULL != p_owner) && (ready(p_owner)) &&
           (BUSLIFE_CLOSED != p_owner->phase) &&
           (p_owner->p_operations->p_owner(p_owner->p_context));
}

/** @brief Start one prechecked session under the permanent lock. */
int buslife_start (buslife_t *              p_owner,
                   const void *             p_settings,
                   const buslife_errors_t * p_errors)
{
    const int locked = claim_lock(p_owner, p_errors);
    if (0 != locked)
    {
        return locked;
    }
    const int started =
        p_owner->p_operations->p_start(p_owner->p_context, p_settings);
    if (0 == started)
    {
        p_owner->phase = BUSLIFE_OPEN;
    }
    return release(p_owner, started, p_errors);
}

/** @brief Stop a prechecked quiescent session, retaining failed cleanup. */
int buslife_stop (buslife_t * p_owner, const buslife_errors_t * p_errors)
{
    const int locked = claim_lock(p_owner, p_errors);
    if (0 != locked)
    {
        return locked;
    }
    /* Block transfers before touching hardware, whatever stop reports. */
    p_owner->phase    = BUSLIFE_RETAINED;
    const int stopped = p_owner->p_operations->p_stop(p_owner->p_context);
    if (0 == stopped)
    {
        p_owner->phase = BUSLIFE_CLOSED;
    }
    return release(p_owner, stopped, p_errors);
}

/** @brief Hold the lock for one transfer only while the session is open. */
int buslife_begin (buslife_t *              p_owner,
                   uint32_t                 timeout_ms,
                   const buslife_errors_t * p_errors)
{
    /* Status changes only during startup, before any transfer can run. */
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return p_errors->state;
    }
    const int locked =
        p_owner->p_operations->p_lock(p_owner->p_context, timeout_ms);
    if (0 != locked)
    {
        return locked;
    }
    return (BUSLIFE_OPEN == p_owner->phase)
               ? 0
               : release(p_owner, p_errors->state, p_errors);
}

/** @brief Release the lock acquired by a successful buslife_begin. */
int buslife_end (buslife_t * p_owner, const buslife_errors_t * p_errors)
{
    if ((NULL == p_owner) || (!ready(p_owner)))
    {
        return p_errors->state;
    }
    return release(p_owner, 0, p_errors);
}

/** @brief Give clock and delay callbacks a cheap session hint. */
bool buslife_is_open (const buslife_t * p_owner)
{
    return (NULL != p_owner) && (ready(p_owner)) &&
           (BUSLIFE_OPEN == p_owner->phase);
}

/** @brief Require every backend callback before creating anything. */
static bool valid_operations (const buslife_ops_t * p_operations)
{
    return (NULL != p_operations) && (NULL != p_operations->p_create) &&
           (NULL != p_operations->p_owner) && (NULL != p_operations->p_lock) &&
           (NULL != p_operations->p_unlock) &&
           (NULL != p_operations->p_start) && (NULL != p_operations->p_stop);
}

/** @brief Accept only a successfully created permanent lock. */
static bool ready (const buslife_t * p_owner)
{
    return BUSLIFE_READY == p_owner->status;
}

/** @brief Poll once; a transfer holding the lock means the bus is in use. */
static int claim_lock (const buslife_t *        p_owner,
                       const buslife_errors_t * p_errors)
{
    const int locked = p_owner->p_operations->p_lock(p_owner->p_context, 0U);
    return (p_errors->timeout == locked) ? p_errors->state : locked;
}

/** @brief Unlock, reporting an unlock failure over the operation's result. */
static int release (const buslife_t *        p_owner,
                    int                      result,
                    const buslife_errors_t * p_errors)
{
    if (0 != p_owner->p_operations->p_unlock(p_owner->p_context))
    {
        return p_errors->io_error;
    }
    return result;
}

/* End of file. */

