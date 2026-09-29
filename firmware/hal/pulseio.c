/**
 * @file pulseio.c
 * @brief Enforce ownership, bounded acquisition and unusable failed outputs.
 */
#include "pulseio.h"
static bool ready (const pulseio_t * p_port, const void * p_owner);

/** @brief Claim before calling a backend which can partly acquire resources. */
int pulseio_open (pulseio_t *              p_port,
                  const pulseio_config_t * p_config,
                  const void *             p_owner)
{
    if ((NULL == p_port) || (NULL == p_config) || (NULL == p_owner) ||
        (NULL == p_port->p_operations) || (NULL != p_port->p_owner) ||
        (p_config->trigger_gpio == p_config->input_gpio))
    {
        return PULSEIO_INVALID;
    }
    const pulseio_ops_t * p_ops = p_port->p_operations;
    if ((NULL == p_ops->p_open) || (NULL == p_ops->p_close) ||
        (NULL == p_ops->p_measure) || (NULL == p_ops->p_now))
    {
        return PULSEIO_INVALID;
    }
    p_port->p_owner  = p_owner;
    const int result = p_ops->p_open(p_port->p_context, p_config);
    p_port->b_ready  = (0 == result);
    return result;
}

/** @brief A failed cleanup keeps the port unavailable to another instance. */
int pulseio_close (pulseio_t * p_port, const void * p_owner)
{
    if ((NULL == p_port) || (NULL == p_owner))
    {
        return PULSEIO_INVALID;
    }
    if (NULL == p_port->p_owner)
    {
        return 0;
    }
    if (p_owner != p_port->p_owner)
    {
        return PULSEIO_STATE;
    }
    p_port->b_ready  = false;
    const int result = p_port->p_operations->p_close(p_port->p_context);
    if (0 == result)
    {
        p_port->p_owner = NULL;
    }
    return result;
}

/** @brief Bound generic capture parameters independently of sensor policy. */
int pulseio_measure (pulseio_t *  p_port,
                     const void * p_owner,
                     uint32_t     trigger_us,
                     uint32_t     timeout_us,
                     uint32_t *   p_high_us)
{
    if (NULL != p_high_us)
    {
        *p_high_us = 0U;
    }
    if ((!ready(p_port, p_owner)) || (NULL == p_high_us) || (trigger_us < 1U) ||
        (trigger_us > 100U) || (timeout_us < 100U) || (timeout_us > 30000U) ||
        (trigger_us >= timeout_us))
    {
        return PULSEIO_INVALID;
    }
    const int result = p_port->p_operations->p_measure(
        p_port->p_context, trigger_us, timeout_us, p_high_us);
    if ((0 != result) || (0U == *p_high_us) || (*p_high_us > timeout_us))
    {
        *p_high_us = 0U;
        return (0 == result) ? PULSEIO_IO : result;
    }
    return 0;
}

/** @brief Read the common monotonic millisecond sample clock. */
int pulseio_now (pulseio_t * p_port, const void * p_owner, uint64_t * p_now_ms)
{
    if ((!ready(p_port, p_owner)) || (NULL == p_now_ms))
    {
        return PULSEIO_STATE;
    }
    return p_port->p_operations->p_now(p_port->p_context, p_now_ms);
}

/** @brief Verify that this borrowed owner completed setup. */
static bool ready (const pulseio_t * p_port, const void * p_owner)
{
    return (NULL != p_port) && (NULL != p_owner) &&
           (p_owner == p_port->p_owner) && (p_port->b_ready);
}

/* End of file. */

