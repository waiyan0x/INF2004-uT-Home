/**
 * @file uartfake.c
 * @brief Model bounded buffers, explicit time and retained failed cleanup.
 */
#include "uartfake.h"
#include <string.h>
static int open_port (void * p_context, const uartio_config_t * p_config);
static int close_port (void * p_context);
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count);
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count);
static int flush_bytes (void * p_context);
static int now_ms (void * p_context, uint64_t * p_now);
static const uartio_ops_t g_operations = {.p_open  = open_port,
                                          .p_close = close_port,
                                          .p_read  = read_bytes,
                                          .p_write = write_bytes,
                                          .p_flush = flush_bytes,
                                          .p_now   = now_ms};

/** @brief Initialize only unused storage; never erase a live claim. */
void uartfake_init (uartfake_t * p_fake)
{
    if (NULL != p_fake)
    {
        *p_fake                   = (uartfake_t){0};
        p_fake->port.p_operations = &g_operations;
        p_fake->port.p_context    = p_fake;
        p_fake->read_limit        = UTHOME_UART_BYTES;
    }
}

/** @brief Preserve queued bytes and mark overflow when incoming data is lost.
 */
int uartfake_feed (uartfake_t * p_fake, const uint8_t * p_data, size_t size)
{
    if ((NULL == p_fake) || (NULL == p_data) || (!p_fake->b_open))
    {
        return UARTIO_INVALID;
    }
    if (size > (sizeof(p_fake->receive) - p_fake->received))
    {
        p_fake->b_overflow = true;
        return UARTIO_OVERFLOW;
    }
    memcpy(&p_fake->receive[p_fake->received], p_data, size);
    p_fake->received += size;
    return 0;
}

/** @brief Simulate partial setup even when an injected open error is returned.
 */
static int open_port (void * p_context, const uartio_config_t * p_config)
{
    uartfake_t * p_fake = p_context;
    p_fake->config      = *p_config;
    p_fake->b_open      = true;
    return p_fake->open_error;
}

/** @brief Leave resources live until cleanup can succeed. */
static int close_port (void * p_context)
{
    uartfake_t * p_fake = p_context;
    if (0 == p_fake->close_error)
    {
        p_fake->b_open      = false;
        p_fake->received    = 0U;
        p_fake->transmitted = 0U;
    }
    return p_fake->close_error;
}

/** @brief Copy one bounded batch and consume overflow only when observed. */
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count)
{
    uartfake_t * p_fake = p_context;
    ++p_fake->reads;
    if (p_fake->b_overflow)
    {
        p_fake->b_overflow = false;
        return UARTIO_OVERFLOW;
    }
    if (0 != p_fake->read_error)
    {
        return p_fake->read_error;
    }
    size_t count = (capacity < p_fake->received) ? capacity : p_fake->received;
    count        = (count < p_fake->read_limit) ? count : p_fake->read_limit;
    memcpy(p_data, p_fake->receive, count);
    p_fake->received -= count;
    memmove(p_fake->receive, &p_fake->receive[count], p_fake->received);
    *p_count = count;
    return 0;
}

/** @brief Expose TX backpressure without overwriting previously accepted data.
 */
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count)
{
    uartfake_t * p_fake = p_context;
    const size_t space  = sizeof(p_fake->transmit) - p_fake->transmitted;
    const size_t count  = (size < space) ? size : space;
    memcpy(&p_fake->transmit[p_fake->transmitted], p_data, count);
    p_fake->transmitted += count;
    *p_count = count;
    return 0;
}

/** @brief Drop a fixed queued snapshot, with an injectable cleanup failure. */
static int flush_bytes (void * p_context)
{
    uartfake_t * p_fake = p_context;
    if (0 == p_fake->flush_error)
    {
        p_fake->received   = 0U;
        p_fake->b_overflow = false;
    }
    return p_fake->flush_error;
}

/** @brief Tests explicitly advance time instead of sleeping. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    const uartfake_t * p_fake = p_context;
    *p_now                    = p_fake->now_ms;
    return 0;
}

/* End of file. */

