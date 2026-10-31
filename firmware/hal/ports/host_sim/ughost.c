#include "ughost.h"
#include <string.h>

static uint64_t now (void * p_context)
{
    return ((ughost_t *)p_context)->now;
}

static ugio_status_t uart_open (void *                     p_context,
                                const ugio_uart_config_t * p_config)
{
    if (((NULL == p_config)) || ((0U == p_config->baud_rate)) ||
        ((0U == p_config->rx_buffer_bytes)) ||
        (p_config->rx_buffer_bytes > UG_HOST_BUFFER) ||
        (p_config->rx_pin == p_config->tx_pin) ||
        (p_config->rx_pin >= UG_HOST_PINS) ||
        (p_config->tx_pin >= UG_HOST_PINS))
    {
        return UG_INVALID;
    }
    ughost_t * p_host     = p_context;
    p_host->b_uart_opened = true;
    p_host->rx_capacity   = p_config->rx_buffer_bytes;
    p_host->rx_len = p_host->rx_pos = p_host->tx_len = 0;
    p_host->b_overflow                               = false;
    return UG_OK;
}

static ugio_status_t uart_read (void *    p_context,
                                uint8_t * p_destination,
                                size_t    capacity,
                                size_t *  p_count,
                                uint32_t  budget)
{
    ughost_t * p_host = p_context;
    if ((NULL == p_count))
    {
        return UG_INVALID;
    }
    *p_count = 0;
    if (((NULL == p_destination)) || ((0U == capacity)) ||
        (!p_host->b_uart_opened))
    {
        return UG_INVALID;
    }
    if (p_host->b_overflow)
    {
        p_host->b_overflow = false;
        return UG_OVERFLOW;
    }
    size_t available = p_host->rx_len - p_host->rx_pos;
    if ((0U == available))
    {
        p_host->now += budget;
        return UG_TIMEOUT;
    }
    *p_count = available < capacity ? available : capacity;
    memcpy(p_destination, p_host->receive + p_host->rx_pos, *p_count);
    p_host->rx_pos += *p_count;
    return UG_OK;
}

static ugio_status_t uart_write (void *          p_context,
                                 const uint8_t * p_source,
                                 size_t          len,
                                 size_t *        p_count,
                                 uint32_t        budget)
{
    ughost_t * p_host = p_context;
    (void)budget;
    if ((NULL == p_count))
    {
        return UG_INVALID;
    }
    *p_count = 0;
    if (((NULL == p_source)) || (!p_host->b_uart_opened))
    {
        return UG_INVALID;
    }
    size_t room = UG_HOST_BUFFER - p_host->tx_len;
    *p_count    = len < room ? len : room;
    memcpy(p_host->transmit + p_host->tx_len, p_source, *p_count);
    p_host->tx_len += *p_count;
    return *p_count == len ? UG_OK : UG_OVERFLOW;
}

static ugio_status_t
config (void * p_context, unsigned pin, ugio_direction_t dir, bool b_initial)
{
    ughost_t * p_host = p_context;
    if ((pin >= UG_HOST_PINS) || (((UG_INPUT != dir) && (UG_OUTPUT != dir))))
    {
        return UG_INVALID;
    }
    p_host->b_configured[pin] = true;
    p_host->direction[pin]    = dir;
    p_host->b_level[pin]      = b_initial;
    return UG_OK;
}

static ugio_status_t read_pin (void * p_context, unsigned pin, bool * pb_value)
{
    ughost_t * p_host = p_context;
    if ((pin >= UG_HOST_PINS) || ((NULL == pb_value)) ||
        (!p_host->b_configured[pin]))
    {
        return UG_INVALID;
    }
    *pb_value = p_host->b_level[pin];
    return UG_OK;
}

static ugio_status_t write_pin (void * p_context, unsigned pin, bool b_value)
{
    ughost_t * p_host = p_context;
    if ((pin >= UG_HOST_PINS) || (!p_host->b_configured[pin]) ||
        (UG_OUTPUT != p_host->direction[pin]))
    {
        return UG_INVALID;
    }
    p_host->b_level[pin] = b_value;
    return UG_OK;
}

static ugio_status_t irq (void *           p_context,
                          unsigned         pin,
                          ugio_edge_t      edge,
                          ugio_edge_fn_t * p_callback,
                          void *           p_arg)
{
    ughost_t * p_host = p_context;
    if ((pin >= UG_HOST_PINS) || (!p_host->b_configured[pin]) ||
        (UG_INPUT != p_host->direction[pin]) ||
        (((UG_RISING != edge) && (UG_FALLING != edge) && (UG_BOTH != edge))))
    {
        return UG_INVALID;
    }
    p_host->p_callbacks[pin]     = p_callback;
    p_host->p_callback_args[pin] = p_arg;
    p_host->edges[pin]           = edge;
    return UG_OK;
}

static ugio_status_t capture (void *     p_context,
                              unsigned   trig,
                              unsigned   echo,
                              uint32_t   pulse,
                              uint32_t   budget,
                              uint32_t * p_high)
{
    ughost_t * p_host = p_context;
    if (((NULL == p_high)) || ((0U == pulse)) || (trig == echo) ||
        (trig >= UG_HOST_PINS) || (echo >= UG_HOST_PINS) ||
        (!p_host->b_configured[trig]) || (!p_host->b_configured[echo]) ||
        (UG_OUTPUT != p_host->direction[trig]) ||
        (UG_INPUT != p_host->direction[echo]))
    {
        return UG_INVALID;
    }
    p_host->b_level[trig] = false;
    if (UG_OK != p_host->capture_status)
    {
        if (UG_TIMEOUT == p_host->capture_status)
        {
            p_host->now += budget;
        }
        return p_host->capture_status;
    }
    uint64_t total =
        (uint64_t)pulse + p_host->echo_delay_us + p_host->echo_high_us;
    if (((0U == p_host->echo_high_us)) || (total > budget))
    {
        p_host->now += budget;
        return UG_TIMEOUT;
    }
    p_host->now += total;
    *p_high = p_host->echo_high_us;
    return UG_OK;
}

static ugio_status_t
pwm (void * p_context, unsigned pin, uint32_t frequency_hz, uint16_t duty)
{
    ughost_t * p_host = p_context;
    if ((pin >= UG_HOST_PINS) || ((0U == frequency_hz)) || (duty > 1000))
    {
        return UG_INVALID;
    }
    p_host->duty[pin]      = duty;
    p_host->frequency[pin] = frequency_hz;
    return UG_OK;
}

void ughost_init (ughost_t * p_host, ugio_t * p_io)
{
    memset(p_host, 0, sizeof(*p_host));
    *p_io = (ugio_t){p_host,
                     now,
                     uart_open,
                     uart_read,
                     uart_write,
                     config,
                     read_pin,
                     write_pin,
                     irq,
                     capture,
                     pwm};
}

ugio_status_t
ughost_receive (ughost_t * p_host, const uint8_t * p_bytes, size_t count)
{
    if (((NULL == p_host)) || ((NULL == p_bytes)) || (!p_host->b_uart_opened))
    {
        return UG_INVALID;
    }
    size_t remaining = p_host->rx_len - p_host->rx_pos;
    if (count > p_host->rx_capacity - remaining)
    {
        p_host->b_overflow = true;
        p_host->rx_len = p_host->rx_pos = 0;
        return UG_OVERFLOW;
    }
    memmove(p_host->receive, p_host->receive + p_host->rx_pos, remaining);
    memcpy(p_host->receive + remaining, p_bytes, count);
    p_host->rx_pos = 0;
    p_host->rx_len = remaining + count;
    return UG_OK;
}

ugio_status_t ughost_input (ughost_t * p_host, unsigned pin, bool b_level)
{
    if (((NULL == p_host)) || (pin >= UG_HOST_PINS) ||
        (!p_host->b_configured[pin]) || (UG_INPUT != p_host->direction[pin]))
    {
        return UG_INVALID;
    }
    bool b_changed       = p_host->b_level[pin] != b_level;
    p_host->b_level[pin] = b_level;
    if ((b_changed) && ((NULL != p_host->p_callbacks[pin])) &&
        (((UG_BOTH == p_host->edges[pin]) ||
          (p_host->edges[pin] == (b_level ? UG_RISING : UG_FALLING)))))
    {
        p_host->p_callbacks[pin](
            p_host->p_callback_args[pin], b_level, p_host->now);
    }
    return UG_OK;
}

/* End of file. */

