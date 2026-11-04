/**
 * @file spiown.c
 * @brief Separate permanent SPI lock creation from repeated bus sessions.
 */
#include "spiown.h"

#include <stddef.h>

static const buslife_errors_t g_errors = {.invalid  = SPIBUS_INVALID,
                                          .state    = SPIBUS_STATE,
                                          .io_error = SPIBUS_IO,
                                          .timeout  = SPIBUS_TIMEOUT};

/** @brief Create the permanent lock once; repeated calls create nothing. */
int spiown_init (spiown_t *           p_owner,
                 const spiown_ops_t * p_operations,
                 void *               p_context)
{
    return buslife_init(p_owner, p_operations, p_context, &g_errors);
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
    if ((!buslife_can_open(p_owner)) || (spibus_has_devices(p_bus)))
    {
        return SPIBUS_STATE;
    }
    return buslife_start(p_owner, p_settings, &g_errors);
}

/** @brief End a quiescent session; failed cleanup stays retained for retry. */
int spiown_close (spiown_t * p_owner, const spibus_t * p_bus)
{
    if ((NULL == p_owner) || (NULL == p_bus))
    {
        return SPIBUS_INVALID;
    }
    if ((!buslife_can_close(p_owner)) || (spibus_has_devices(p_bus)))
    {
        return SPIBUS_STATE;
    }
    return buslife_stop(p_owner, &g_errors);
}

/** @brief Hold the lock for one transfer only while the session is open. */
int spiown_begin (spiown_t * p_owner, uint32_t timeout_ms)
{
    return buslife_begin(p_owner, timeout_ms, &g_errors);
}

/** @brief Release the lock acquired by a successful spiown_begin. */
int spiown_end (spiown_t * p_owner)
{
    return buslife_end(p_owner, &g_errors);
}

/** @brief Report the session phase without the lifecycle-owner check. */
bool spiown_is_open (const spiown_t * p_owner)
{
    return buslife_is_open(p_owner);
}

/* End of file. */

