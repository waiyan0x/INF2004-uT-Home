/**
 * @file ld2450.c
 * @brief Bound stream processing and invalidate damaged or stale fragments.
 */
#include "ld2450.h"
static int poll_bytes (ld2450_t *               p_sensor,
                       uint64_t                 now_ms,
                       sample_target_report_t * p_report);

/** @brief Retain the port even when setup or initial flushing fails. */
int ld2450_open (ld2450_t *              p_sensor,
                 uartio_t *              p_port,
                 const uartio_config_t * p_config,
                 uint32_t                timeout_ms)
{
    if ((NULL == p_sensor) || (NULL == p_port) || (NULL == p_config) ||
        (NULL != p_sensor->p_port) || (8U != p_config->data_bits) ||
        (1U != p_config->stop_bits) || (0U != p_config->parity) ||
        (p_config->rx_buffer_bytes < 512U) || (timeout_ms < 200U) ||
        (timeout_ms > 500U))
    {
        return UARTIO_INVALID;
    }
    p_sensor->p_port = p_port;
    int result       = uartio_open(p_port, p_config, p_sensor);
    if (0 == result)
    {
        result = uartio_flush(p_port, p_sensor);
    }
    if (0 == result)
    {
        result = uartio_now(p_port, p_sensor, &p_sensor->last_report_ms);
    }
    if (0 == result)
    {
        p_sensor->timeout_ms = timeout_ms;
        p_sensor->b_ready    = true;
    }
    return result;
}

/** @brief Finish promptly even if input is absent, malformed or continuous. */
int ld2450_poll (ld2450_t * p_sensor, sample_target_report_t * p_report)
{
    if (NULL != p_report)
    {
        *p_report = (sample_target_report_t){0};
    }
    if ((NULL == p_sensor) || (NULL == p_report) || (!p_sensor->b_ready))
    {
        return UARTIO_STATE;
    }
    uint64_t now_ms = 0U;
    if ((0 != uartio_now(p_sensor->p_port, p_sensor, &now_ms)) ||
        (now_ms < p_sensor->last_report_ms))
    {
        return UARTIO_STATE;
    }
    p_report->header.timestamp_ms = now_ms;
    if ((now_ms - p_sensor->last_report_ms) >= p_sensor->timeout_ms)
    {
        ldparse_reset(&p_sensor->parser);
        p_sensor->b_flush        = true;
        p_sensor->last_report_ms = now_ms;
        ++p_sensor->timeouts;
        return UARTIO_TIMEOUT;
    }
    if (p_sensor->b_flush)
    {
        const int flushed = uartio_flush(p_sensor->p_port, p_sensor);
        if (0 != flushed)
        {
            return flushed;
        }
        p_sensor->b_flush = false;
        return UARTIO_AGAIN;
    }
    return poll_bytes(p_sensor, now_ms, p_report);
}

/** @brief Failed claims on another instance never close that instance. */
int ld2450_close (ld2450_t * p_sensor)
{
    if (NULL == p_sensor)
    {
        return UARTIO_INVALID;
    }
    int result        = 0;
    p_sensor->b_ready = false;
    if ((NULL != p_sensor->p_port) && (p_sensor == p_sensor->p_port->p_owner))
    {
        result = uartio_close(p_sensor->p_port, p_sensor);
    }
    if (0 == result)
    {
        *p_sensor = (ld2450_t){0};
    }
    return result;
}

/** @brief Do not parse any bytes from a transfer with an observed RX fault. */
static int poll_bytes (ld2450_t *               p_sensor,
                       uint64_t                 now_ms,
                       sample_target_report_t * p_report)
{
    uint8_t   bytes[UTHOME_UART_BYTES] = {0};
    size_t    count                    = 0U;
    const int result =
        uartio_read(p_sensor->p_port, p_sensor, bytes, sizeof(bytes), &count);
    if (result < 0)
    {
        ldparse_reset(&p_sensor->parser);
        p_sensor->b_flush = true;
        if (UARTIO_OVERFLOW == result)
        {
            ++p_sensor->overflows;
        }
        else
        {
            ++p_sensor->io_errors;
        }
        return result;
    }
    if (UARTIO_AGAIN == result)
    {
        return result;
    }
    uint64_t completed_ms = 0U;
    if ((0 != uartio_now(p_sensor->p_port, p_sensor, &completed_ms)) ||
        (completed_ms < now_ms))
    {
        return UARTIO_STATE;
    }
    bool           b_complete = false;
    const uint32_t errors     = p_sensor->parser.framing_errors;
    for (size_t index = 0U; index < count; ++index)
    {
        if (ldparse_feed(
                &p_sensor->parser, bytes[index], completed_ms, p_report))
        {
            b_complete = true;
        }
    }
    if (b_complete)
    {
        p_sensor->last_report_ms = completed_ms;
        return 0;
    }
    return (errors != p_sensor->parser.framing_errors) ? UARTIO_FRAME
                                                       : UARTIO_AGAIN;
}

/* End of file. */

