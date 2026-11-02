/**
 * @file i2cown.c
 * @brief Separate permanent lock creation from repeated bus sessions.
 */
#include "i2cown.h"

#include <stddef.h>

static const buslife_errors_t g_errors = {.invalid  = I2CBUS_INVALID,
                                          .state    = I2CBUS_STATE,
                                          .io_error = I2CBUS_IO,
                                          .timeout  = I2CBUS_TIMEOUT};

/** @brief Create the permanent lock once; repeated calls create nothing. */
int i2cown_init (i2cown_t *           p_owner,
                 const i2cown_ops_t * p_operations,
                 void *               p_context)
{
    return buslife_init(p_owner, p_operations, p_context, &g_errors);
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
    if (!buslife_can_open(p_owner))
    {
        return I2CBUS_STATE;
    }
    return buslife_start(p_owner, p_config, &g_errors);
}

/** @brief End a quiescent session; failed cleanup stays retained for retry. */
int i2cown_close (i2cown_t * p_owner, const i2cbus_t * p_bus)
{
    if ((NULL == p_owner) || (NULL == p_bus))
    {
        return I2CBUS_INVALID;
    }
    if ((!buslife_can_close(p_owner)) || (i2cbus_has_claims(p_bus)))
    {
        return I2CBUS_STATE;
    }
    return buslife_stop(p_owner, &g_errors);
}

/** @brief Hold the lock for one transfer only while the session is open. */
int i2cown_begin (i2cown_t * p_owner, uint32_t timeout_ms)
{
    return buslife_begin(p_owner, timeout_ms, &g_errors);
}

/** @brief Release the lock acquired by a successful i2cown_begin. */
int i2cown_end (i2cown_t * p_owner)
{
    return buslife_end(p_owner, &g_errors);
}

/** @brief Give clock and delay callbacks a cheap session hint. */
bool i2cown_is_open (const i2cown_t * p_owner)
{
    return buslife_is_open(p_owner);
}

/* End of file. */

