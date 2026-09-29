/**
 * @file uartio.c
 * @brief Keep byte transports independent of protocols and runtime names.
 */
#include "uartio.h"
static bool ready (const uartio_t * p_port, const void * p_owner);

/** @brief Retain ownership before entering a backend that may partly open. */
int uartio_open (uartio_t *              p_port,
                 const uartio_config_t * p_config,
                 const void *            p_owner)
{
    if ((NULL == p_port) || (NULL == p_config) || (NULL == p_owner) ||
        (NULL == p_port->p_operations) || (NULL != p_port->p_owner) ||
        (0U == p_config->baud) || (0U == p_config->rx_buffer_bytes) ||
        (p_config->tx_gpio == p_config->rx_gpio) ||
        (p_config->data_bits < 5U) || (p_config->data_bits > 8U) ||
        (p_config->stop_bits < 1U) || (p_config->stop_bits > 2U) ||
        (p_config->parity > 2U))
    {
        return UARTIO_INVALID;
    }
    const uartio_ops_t * p_ops = p_port->p_operations;
    if ((NULL == p_ops->p_open) || (NULL == p_ops->p_close) ||
        (NULL == p_ops->p_read) || (NULL == p_ops->p_write) ||
        (NULL == p_ops->p_flush) || (NULL == p_ops->p_now))
    {
        return UARTIO_INVALID;
    }
    p_port->p_owner  = p_owner;
    const int result = p_ops->p_open(p_port->p_context, p_config);
    p_port->b_ready  = (0 == result);
    return result;
}

/** @brief Never release a claim before backend cleanup succeeds. */
int uartio_close (uartio_t * p_port, const void * p_owner)
{
    if ((NULL == p_port) || (NULL == p_owner))
    {
        return UARTIO_INVALID;
    }
    if (NULL == p_port->p_owner)
    {
        return 0;
    }
    if (p_owner != p_port->p_owner)
    {
        return UARTIO_STATE;
    }
    p_port->b_ready  = false;
    const int result = p_port->p_operations->p_close(p_port->p_context);
    if (0 == result)
    {
        p_port->p_owner = NULL;
    }
    return result;
}

/** @brief Return no usable bytes on errors or backend contract violations. */
int uartio_read (uartio_t *   p_port,
                 const void * p_owner,
                 uint8_t *    p_data,
                 size_t       capacity,
                 size_t *     p_count)
{
    if (NULL != p_count)
    {
        *p_count = 0U;
    }
    if ((!ready(p_port, p_owner)) || (NULL == p_data) || (NULL == p_count) ||
        (0U == capacity) || (capacity > UTHOME_UART_BYTES))
    {
        return UARTIO_INVALID;
    }
    const int result = p_port->p_operations->p_read(
        p_port->p_context, p_data, capacity, p_count);
    if ((0 != result) || (*p_count > capacity))
    {
        *p_count = 0U;
        return (0 == result) ? UARTIO_IO : result;
    }
    return (0U == *p_count) ? UARTIO_AGAIN : 0;
}

/** @brief Preserve partial acceptance so a caller never retransmits it. */
int uartio_write (uartio_t *      p_port,
                  const void *    p_owner,
                  const uint8_t * p_data,
                  size_t          size,
                  size_t *        p_count)
{
    if (NULL != p_count)
    {
        *p_count = 0U;
    }
    if ((!ready(p_port, p_owner)) || (NULL == p_data) || (NULL == p_count) ||
        (0U == size) || (size > UTHOME_UART_BYTES))
    {
        return UARTIO_INVALID;
    }
    const int result =
        p_port->p_operations->p_write(p_port->p_context, p_data, size, p_count);
    if (*p_count > size)
    {
        *p_count = 0U;
        return UARTIO_IO;
    }
    return ((0 == result) && (0U == *p_count)) ? UARTIO_AGAIN : result;
}

/** @brief Discard a bounded snapshot without waiting for a quiet wire. */
int uartio_flush (uartio_t * p_port, const void * p_owner)
{
    return ready(p_port, p_owner)
               ? p_port->p_operations->p_flush(p_port->p_context)
               : UARTIO_STATE;
}

/** @brief Use the same monotonic millisecond origin as other samples. */
int uartio_now (uartio_t * p_port, const void * p_owner, uint64_t * p_now)
{
    if ((!ready(p_port, p_owner)) || (NULL == p_now))
    {
        return UARTIO_INVALID;
    }
    return p_port->p_operations->p_now(p_port->p_context, p_now);
}

/** @brief Lifecycle and transfer access share one borrowed identity. */
static bool ready (const uartio_t * p_port, const void * p_owner)
{
    return (NULL != p_port) && (NULL != p_owner) &&
           (p_owner == p_port->p_owner) && (p_port->b_ready);
}

/* End of file. */

