/**
 * @file hc_sr04.c
 * @brief Enforce acoustic spacing and reject invalid echo widths.
 */
#include "hc_sr04.h"

/** @brief Retain ownership through any partially completed port setup. */
int hc_sr04_open (hc_sr04_t *              p_sensor,
                  pulseio_t *              p_port,
                  const pulseio_config_t * p_config,
                  uint32_t                 timeout_us)
{
    if ((NULL == p_sensor) || (NULL == p_port) || (NULL == p_config) ||
        (NULL != p_sensor->p_port) || (timeout_us < 25000U) ||
        (timeout_us > 30000U))
    {
        return PULSEIO_INVALID;
    }
    p_sensor->p_port = p_port;
    const int result = pulseio_open(p_port, p_config, p_sensor);
    if (0 == result)
    {
        p_sensor->timeout_us = timeout_us;
        p_sensor->b_ready    = true;
    }
    return result;
}

/** @brief A total capture deadline covers the trigger and both echo edges. */
int hc_sr04_read (hc_sr04_t * p_sensor, hc_sr04_value_t * p_value)
{
    if (NULL != p_value)
    {
        *p_value = (hc_sr04_value_t){0};
    }
    if ((NULL == p_sensor) || (NULL == p_value) || (!p_sensor->b_ready))
    {
        return PULSEIO_STATE;
    }
    uint64_t now_ms = 0U;
    if ((0 != pulseio_now(p_sensor->p_port, p_sensor, &now_ms)) ||
        ((p_sensor->b_started) && (now_ms < p_sensor->last_finish_ms)))
    {
        p_sensor->b_ready = false;
        return PULSEIO_STATE;
    }
    if ((p_sensor->b_started) && ((now_ms - p_sensor->last_finish_ms) < 62U))
    {
        return PULSEIO_BUSY;
    }
    p_sensor->b_started = true;
    uint32_t  high_us   = 0U;
    const int result    = pulseio_measure(
        p_sensor->p_port, p_sensor, 10U, p_sensor->timeout_us, &high_us);
    uint64_t finished_ms = 0U;
    if ((0 != pulseio_now(p_sensor->p_port, p_sensor, &finished_ms)) ||
        (finished_ms < now_ms))
    {
        /* Unknown elapsed time cannot authorize another acoustic trigger. */
        p_sensor->b_ready = false;
        return PULSEIO_STATE;
    }
    p_sensor->last_finish_ms = finished_ms;
    if (0 != result)
    {
        return result;
    }
    /* high_us <=30000, so multiplication and rounding fit uint32_t. */
    const uint32_t distance_mm = ((high_us * 10U) + 29U) / 58U;
    if ((distance_mm < 20U) || (distance_mm > 4000U))
    {
        return PULSEIO_INVALID;
    }
    *p_value =
        (hc_sr04_value_t){.distance_mm = distance_mm, .echo_us = high_us};
    return 0;
}

/** @brief A failed duplicate open must never release another sensor's port. */
int hc_sr04_close (hc_sr04_t * p_sensor)
{
    if (NULL == p_sensor)
    {
        return PULSEIO_INVALID;
    }
    p_sensor->b_ready = false;
    int result        = 0;
    if ((NULL != p_sensor->p_port) && (p_sensor == p_sensor->p_port->p_owner))
    {
        result = pulseio_close(p_sensor->p_port, p_sensor);
    }
    if (0 == result)
    {
        *p_sensor = (hc_sr04_t){0};
    }
    return result;
}

/* End of file. */

