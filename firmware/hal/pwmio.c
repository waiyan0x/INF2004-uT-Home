/**
 * @file pwmio.c
 * @brief Retain an output until safe shutdown succeeds.
 */
#include "pwmio.h"

/** @brief Only the backend may enable the pin, initially at zero duty. */
int pwmio_open (pwmio_t *              p_port,
                const pwmio_config_t * p_config,
                const void *           p_owner)
{
    if ((NULL == p_port) || (NULL == p_config) || (NULL == p_owner) ||
        (NULL == p_port->p_operations) || (NULL != p_port->p_owner) ||
        (0U == p_config->frequency_hz))
    {
        return PWMIO_INVALID;
    }
    const pwmio_ops_t * p_ops = p_port->p_operations;
    if ((NULL == p_ops->p_open) || (NULL == p_ops->p_close) ||
        (NULL == p_ops->p_set))
    {
        return PWMIO_INVALID;
    }
    p_port->p_owner       = p_owner;
    p_port->duty_permille = 0U;
    const int result      = p_ops->p_open(p_port->p_context, p_config);
    p_port->b_ready       = (0 == result);
    return result;
}

/** @brief Reject malformed commands before touching the backend. */
int pwmio_set (pwmio_t * p_port, const void * p_owner, uint16_t duty_permille)
{
    if ((NULL == p_port) || (NULL == p_owner) || (p_owner != p_port->p_owner) ||
        (!p_port->b_ready) || (duty_permille > 1000U))
    {
        return PWMIO_INVALID;
    }
    const int result =
        p_port->p_operations->p_set(p_port->p_context, duty_permille);
    if (0 == result)
    {
        p_port->duty_permille = duty_permille;
    }
    return result;
}

/** @brief Backend cleanup must drive low even after partially failed setup. */
int pwmio_close (pwmio_t * p_port, const void * p_owner)
{
    if ((NULL == p_port) || (NULL == p_owner))
    {
        return PWMIO_INVALID;
    }
    if (NULL == p_port->p_owner)
    {
        return 0;
    }
    if (p_owner != p_port->p_owner)
    {
        return PWMIO_STATE;
    }
    p_port->b_ready  = false;
    const int result = p_port->p_operations->p_close(p_port->p_context);
    if (0 == result)
    {
        p_port->p_owner       = NULL;
        p_port->duty_permille = 0U;
    }
    return result;
}

/* End of file. */

