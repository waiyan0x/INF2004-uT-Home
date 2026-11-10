/**
 * @file txport.c
 * @brief Execute bounded raw I2C, write-only SPI and incremental UART I/O.
 */
#include "txport.h"
#include <string.h>

static txjob_status_t uart_step (txport_t * p_port, uint64_t now_ms);
static txjob_status_t status (int result, int timeout);

/** @brief Bind only fixed reviewed board buses, preserving existing ownership.
 */
txjob_status_t
txport_begin (void * p_context, const txjob_rule_t * p_rule, uint64_t now_ms)
{
    txport_t * p_port = p_context;
    if ((NULL == p_port) || (NULL == p_rule) || (NULL == p_port->p_io) ||
        (NULL != p_port->p_i2c) || (NULL != p_port->p_spi) ||
        (NULL != p_port->p_uart) || (NULL != p_port->control_line.p_set) ||
        (now_ms > UINT64_MAX - p_rule->timeout_ms))
    {
        return TXJOB_IO;
    }
    p_port->rule        = *p_rule;
    p_port->deadline_ms = now_ms + p_rule->timeout_ms;
    p_port->written     = 0U;
    p_port->received    = 0U;
    p_port->b_settling  = false;
    memset(p_port->receive, 0, sizeof(p_port->receive));
    if (1U == p_rule->protocol)
    {
        const i2cbus_config_t config = {.port     = 0U,
                                        .sda_gpio = 0U,
                                        .scl_gpio = 1U,
                                        .clock_hz = p_rule->rate};
        int result = p_port->p_io->p_i2c_open(&config, &p_port->p_i2c);
        if (0 == result)
        {
            result = i2cbus_claim(p_port->p_i2c, p_rule->address);
            p_port->b_i2c_claimed = 0 == result;
        }
        return status(result, I2CBUS_TIMEOUT);
    }
    if (2U == p_rule->protocol)
    {
        int result = p_port->p_io->p_spi_open(7U, 6U, &p_port->p_spi);
        if ((0 == result) && (255U != p_rule->dc_pin))
        {
            result = p_port->p_io->p_line_open(7U,
                                               p_rule->dc_pin,
                                               0U != p_rule->dc_level,
                                               &p_port->control_line);
        }
        const spibus_config_t config = {.cs_gpio  = p_rule->cs_pin,
                                        .mode     = p_rule->mode,
                                        .clock_hz = p_rule->rate};
        if (0 == result)
        {
            result = spibus_attach(p_port->p_spi, &p_port->spi, &config);
        }
        if (0 == result)
        {
            result                = spibus_claim(&p_port->spi, p_port);
            p_port->b_spi_claimed = 0 == result;
        }
        return status(result, SPIBUS_TIMEOUT);
    }
    if (3U == p_rule->protocol)
    {
        if ((0U != p_rule->write_size) && (NULL == p_port->p_tx_empty))
        {
            return TXJOB_UNSUPPORTED;
        }
        const uartio_config_t config = {.baud            = p_rule->rate,
                                        .rx_buffer_bytes = 512U,
                                        .port            = 1U,
                                        .tx_gpio         = 21U,
                                        .rx_gpio         = 20U,
                                        .data_bits       = 8U,
                                        .stop_bits       = 1U,
                                        .parity          = 0U};
        p_port->p_uart               = p_port->p_io->p_uart();
        return status(uartio_open(p_port->p_uart, &config, p_port),
                      UARTIO_TIMEOUT);
    }
    return TXJOB_UNSUPPORTED;
}

/** @brief At most one UART read/write per call, with no polling wait loop. */
txjob_status_t txport_poll (void *    p_context,
                            uint64_t  now_ms,
                            uint8_t * p_written,
                            uint8_t * p_data,
                            uint8_t * p_size)
{
    txport_t * p_port = p_context;
    if ((NULL == p_port) || (NULL == p_written) || (NULL == p_data) ||
        (NULL == p_size))
    {
        return TXJOB_IO;
    }
    *p_size    = 0U;
    *p_written = p_port->written;
    if (now_ms >= p_port->deadline_ms)
    {
        return TXJOB_TIMEOUT;
    }
    txjob_status_t result = TXJOB_IO;
    if (1U == p_port->rule.protocol)
    {
        const i2cbus_request_t request = {.address    = p_port->rule.address,
                                          .p_write    = p_port->rule.write,
                                          .write_size = p_port->rule.write_size,
                                          .p_read     = p_port->receive,
                                          .read_size  = p_port->rule.read_size,
                                          .deadline_ms   = p_port->deadline_ms,
                                          .read_pause_ms = 0U};
        result =
            status(i2cbus_transfer(p_port->p_i2c, &request), I2CBUS_TIMEOUT);
        if (TXJOB_OK == result)
        {
            p_port->written  = p_port->rule.write_size;
            p_port->received = p_port->rule.read_size;
        }
    }
    else if (2U == p_port->rule.protocol)
    {
        const spibus_request_t request = {.p_write = p_port->rule.write,
                                          .p_read  = NULL,
                                          .size    = p_port->rule.write_size,
                                          .deadline_ms = p_port->deadline_ms};
        result =
            status(spibus_transfer(&p_port->spi, &request), SPIBUS_TIMEOUT);
        if (TXJOB_OK == result)
        {
            p_port->written = p_port->rule.write_size;
        }
    }
    else if (3U == p_port->rule.protocol)
    {
        result = uart_step(p_port, now_ms);
    }
    else
    {
        /* Unsupported protocols never access a HAL. */
    }
    *p_written = p_port->written;
    if (TXJOB_OK == result)
    {
        *p_size = p_port->received;
        memcpy(p_data, p_port->receive, p_port->received);
    }
    return result;
}

/** @brief Release only this adapter's claims; retain every failed resource. */
int txport_close (void * p_context)
{
    txport_t * p_port = p_context;
    if ((NULL == p_port) || (NULL == p_port->p_io))
    {
        return -1;
    }
    if (p_port->b_i2c_claimed)
    {
        if (0 != i2cbus_release(p_port->p_i2c, p_port->rule.address))
        {
            return -1;
        }
        p_port->b_i2c_claimed = false;
    }
    if (NULL != p_port->p_i2c)
    {
        if (0 != p_port->p_io->p_i2c_close(p_port->p_i2c))
        {
            return -1;
        }
        p_port->p_i2c = NULL;
    }
    if (p_port->b_spi_claimed)
    {
        if (0 != spibus_release(&p_port->spi, p_port))
        {
            return -1;
        }
        p_port->b_spi_claimed = false;
    }
    if (NULL != p_port->spi.p_bus)
    {
        if (0 != spibus_detach(&p_port->spi))
        {
            return -1;
        }
    }
    if (NULL != p_port->control_line.p_set)
    {
        if (0 != p_port->p_io->p_line_close(&p_port->control_line))
        {
            return -1;
        }
    }
    if (NULL != p_port->p_spi)
    {
        if (0 != p_port->p_io->p_spi_close(p_port->p_spi))
        {
            return -1;
        }
        p_port->p_spi = NULL;
    }
    if (NULL != p_port->p_uart)
    {
        if ((NULL != p_port->p_uart->p_owner) &&
            ((p_port != p_port->p_uart->p_owner) ||
             (0 != uartio_close(p_port->p_uart, p_port))))
        {
            return -1;
        }
        p_port->p_uart = NULL;
    }
    return 0;
}

/** @brief Preserve accepted byte counts, including on a later transfer error.
 */
static txjob_status_t uart_step (txport_t * p_port, uint64_t now_ms)
{
    size_t count  = 0U;
    int    result = 0;
    if (p_port->written < p_port->rule.write_size)
    {
        result          = uartio_write(p_port->p_uart,
                              p_port,
                              &p_port->rule.write[p_port->written],
                              p_port->rule.write_size - p_port->written,
                              &count);
        p_port->written = (uint8_t)(p_port->written + count);
        if ((UARTIO_OK != result) && (UARTIO_AGAIN != result))
        {
            return status(result, UARTIO_TIMEOUT);
        }
        return TXJOB_PENDING;
    }
    if (0U != p_port->rule.write_size)
    {
        if (!p_port->b_settling)
        {
            bool b_empty = false;
            if (0 != p_port->p_tx_empty(p_port, &b_empty))
            {
                return TXJOB_IO;
            }
            if (!b_empty)
            {
                return TXJOB_PENDING;
            }
            /* C3 UART has at most 128 FIFO bytes plus one shift-register
             * byte. After its software ring empties, allow their complete
             * 8N1 serialization before stopping UART. No flow control. */
            const uint32_t tail_ms =
                (1290000U + p_port->rule.rate - 1U) / p_port->rule.rate + 2U;
            p_port->settle_ms  = now_ms + tail_ms;
            p_port->b_settling = true;
            return TXJOB_PENDING;
        }
        if (now_ms < p_port->settle_ms)
        {
            return TXJOB_PENDING;
        }
    }
    if (p_port->received < p_port->rule.read_size)
    {
        result = uartio_read(p_port->p_uart,
                             p_port,
                             &p_port->receive[p_port->received],
                             p_port->rule.read_size - p_port->received,
                             &count);
        if ((UARTIO_OK != result) && (UARTIO_AGAIN != result))
        {
            return status(result, UARTIO_TIMEOUT);
        }
        p_port->received = (uint8_t)(p_port->received + count);
    }
    return (p_port->received == p_port->rule.read_size) ? TXJOB_OK
                                                        : TXJOB_PENDING;
}

/** @brief Never confuse HAL-specific numeric error values with wire status. */
static txjob_status_t status (int result, int timeout)
{
    return (0 == result) ? TXJOB_OK
                         : ((timeout == result) ? TXJOB_TIMEOUT : TXJOB_IO);
}

/* End of file. */

