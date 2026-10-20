/**
 * @file discuart.c
 * @brief Listen on one exclusively owned connection without issuing commands.
 */
#include "discuart.h"
#include "ldparse.h"
#include <string.h>
static void collect (discuart_t * p_context, discuart_data_t * p_result);
static int receive (discuart_t *      p_context,
                    ldparse_t *       p_parser,
                    discuart_data_t * p_result,
                    uint64_t          now_ms);

/** @brief Retain a failed close for explicit recovery by the same owner. */
int discuart_release (discuart_t * p_context)
{
    if ((NULL == p_context) || (NULL == p_context->p_port) ||
        (p_context->b_busy))
    {
        return -1;
    }
    if (p_context != p_context->p_port->p_owner)
    {
        return 0;
    }
    return uartio_close(p_context->p_port, p_context);
}

/** @brief Report raw counts only; a complete empty window is not disconnection.
 */
int discuart_run (discuart_t *            p_context,
                  const uartio_config_t * p_config,
                  discuart_data_t *       p_result)
{
    if ((NULL == p_context) || (NULL == p_context->p_port) ||
        (NULL == p_context->p_now) || (NULL == p_context->p_wait) ||
        (NULL == p_config) || (NULL == p_result))
    {
        return -1;
    }
    memset(p_result, 0, sizeof(*p_result));
    p_result->status = DISCUART_BUSY;
    if ((p_context->b_busy) || (NULL != p_context->p_port->p_owner))
    {
        return 0;
    }
    p_context->b_busy = true;
    p_result->status  = DISCUART_FAULT;
    if ((0 == uartio_open(p_context->p_port, p_config, p_context)) &&
        (0 == uartio_flush(p_context->p_port, p_context)) &&
        (0 == p_context->p_now(&p_result->started_ms)))
    {
        collect(p_context, p_result);
    }
    p_context->b_busy = false;
    if (0 != discuart_release(p_context))
    {
        p_result->status = DISCUART_FAULT;
    }
    return 0;
}

/** @brief A finite iteration cap also bounds a stopped or broken clock. */
static void collect (discuart_t * p_context, discuart_data_t * p_result)
{
    ldparse_t parser   = {0};
    uint64_t  previous = p_result->started_ms;
    p_result->status   = DISCUART_INCOMPLETE;
    for (uint32_t iteration = 0U; iteration < 2000U; ++iteration)
    {
        uint64_t now_ms = 0U;
        if ((0 != p_context->p_now(&now_ms)) || (now_ms < previous))
        {
            p_result->status = DISCUART_FAULT;
            break;
        }
        const uint64_t elapsed = now_ms - p_result->started_ms;
        /* A long scheduling gap cannot be represented as a clean window. */
        if (elapsed > 1100U)
        {
            break;
        }
        p_result->elapsed_ms = (uint32_t)elapsed; /* Bounded above by 1100. */
        previous             = now_ms;
        if (elapsed >= 1000U)
        {
            p_result->window_ms = 1000U;
            p_result->status    = DISCUART_COMPLETE;
            break;
        }
        if ((0 != receive(p_context, &parser, p_result, now_ms)) ||
            (0 != p_context->p_wait()))
        {
            p_result->status = DISCUART_FAULT;
            break;
        }
    }
    p_result->valid_reports   = parser.frames;
    p_result->framing_errors  = parser.framing_errors;
    p_result->discarded_bytes = parser.discarded_bytes;
    /* The existing parser retains fewer than one 30-byte report. */
    p_result->pending_bytes = (uint32_t)parser.used;
}

/** @brief Reject an entire batch on receive errors; never use its bytes. */
static int receive (discuart_t *      p_context,
                    ldparse_t *       p_parser,
                    discuart_data_t * p_result,
                    uint64_t          now_ms)
{
    uint8_t   bytes[UTHOME_UART_BYTES] = {0};
    size_t    count                    = 0U;
    const int status =
        uartio_read(p_context->p_port, p_context, bytes, sizeof(bytes), &count);
    if ((0 != status) && (UARTIO_AGAIN != status))
    {
        ++p_result->receive_errors;
        return -1;
    }
    p_result->received_bytes += (uint32_t)count; /* At most 2000 * 64 bytes. */
    sample_target_report_t report = {0};
    for (size_t index = 0U; index < count; ++index)
    {
        /* The parser counts complete frames, including valid zero-target ones.
         * Values are not needed for identification and are never transmitted.
         */
        (void)ldparse_feed(p_parser, bytes[index], now_ms, &report);
    }
    return 0;
}

/* End of file. */

