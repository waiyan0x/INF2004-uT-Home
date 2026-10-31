/**
 * @file uartcheck.c
 * @brief Exercise real native rings and ownership, with one injected read
 * error.
 */
#include "uartcheck.h"
#include "c3uart.h"
#include "ser.h"
static bool           gb_fail_read = false;
static const unsigned g_owner_a    = 1U;
static const unsigned g_owner_b    = 2U;
static int check_cycle (uartio_t * p_port, const uartio_config_t * p_config);

/** @brief Fail exactly one read without replacing normal native behavior. */
ER uartcheck_read (ID h_device, W start, void * p_data, SZ size, SZ * p_count)
{
    if (gb_fail_read)
    {
        gb_fail_read = false;
        *p_count     = 0;
        return E_IO;
    }
    return tk_srea_dev(h_device, start, p_data, size, p_count);
}

/** @brief Run after startup; disconnect external UART senders for this test. */
int uartcheck_run (void)
{
    uartio_t *      p_port  = c3uart_port();
    uartio_config_t config  = {.baud            = 9600U,
                               .rx_buffer_bytes = 512U,
                               .port            = 1U,
                               .tx_gpio         = 21U,
                               .rx_gpio         = 20U,
                               .data_bits       = 8U,
                               .stop_bits       = 1U,
                               .parity          = 0U};
    const uint32_t  rates[] = {9600U, 115200U, 256000U, 460800U};
    for (size_t rate = 0U; rate < (sizeof(rates) / sizeof(rates[0])); ++rate)
    {
        config.baud = rates[rate];
        for (uint8_t bits = 5U; bits <= 8U; ++bits)
        {
            config.data_bits = bits;
            for (uint8_t parity = 0U; parity <= 2U; ++parity)
            {
                config.parity = parity;
                for (uint8_t stop = 1U; stop <= 2U; ++stop)
                {
                    config.stop_bits = stop;
                    if (0 != check_cycle(p_port, &config))
                    {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

/** @brief Verify errors, retained claims and old-data discard for each mode. */
static int check_cycle (uartio_t * p_port, const uartio_config_t * p_config)
{
    uint8_t byte                        = 0U;
    size_t  count                       = 0U;
    uint8_t transmit[UTHOME_UART_BYTES] = {0};
    UW      stale                       = 0U;
    if ((0 != uartio_open(p_port, p_config, &g_owner_a)) ||
        (UARTIO_STATE != uartio_close(p_port, &g_owner_b)) ||
        (UARTIO_INVALID !=
         uartio_read(p_port, &g_owner_b, &byte, 1U, &count)) ||
        (UARTIO_INVALID != uartio_write(p_port, &g_owner_b, &byte, 1U, &count)))
    {
        return -1;
    }
    /* No external sender: synchronously inject into the actual native ring. */
    for (size_t index = 0U; index < (DEVCONF_SER_BUFFSIZE + 8U); ++index)
    {
        dev_ser_notify_rcv(0U, 0x5AU);
    }
    if ((UARTIO_OVERFLOW !=
         uartio_read(p_port, &g_owner_a, &byte, 1U, &count)) ||
        (0U != count) || (0 != uartio_flush(p_port, &g_owner_a)))
    {
        return -1;
    }
    dev_ser_notify_err(0U, DEV_SER_ERR_FE);
    if ((UARTIO_FRAME != uartio_read(p_port, &g_owner_a, &byte, 1U, &count)) ||
        (0U != count) ||
        (0 != uartio_write(
                  p_port, &g_owner_a, transmit, sizeof(transmit), &count)) ||
        (sizeof(transmit) != count))
    {
        return -1;
    }
    dev_ser_notify_rcv(0U, 0xA5U);
    gb_fail_read = true;
    if ((UARTIO_IO != uartio_close(p_port, &g_owner_a)) || (gb_fail_read) ||
        (p_port->p_owner != &g_owner_a) ||
        (UARTIO_INVALID != uartio_open(p_port, p_config, &g_owner_b)) ||
        (UARTIO_INVALID != uartio_read(p_port, &g_owner_a, &byte, 1U, &count)))
    {
        return -1;
    }
    if ((0 != uartio_close(p_port, &g_owner_a)) ||
        (dev_ser_get_snddat(0U, &stale)) ||
        (0 != uartio_open(p_port, p_config, &g_owner_b)) ||
        (UARTIO_AGAIN != uartio_read(p_port, &g_owner_b, &byte, 1U, &count)) ||
        (0U != count))
    {
        return -1;
    }
    dev_ser_notify_rcv(0U, 0x3CU);
    if ((0 != uartio_read(p_port, &g_owner_b, &byte, 1U, &count)) ||
        (1U != count) || (0x3CU != byte))
    {
        return -1;
    }
    return uartio_close(p_port, &g_owner_b);
}

/* End of file. */

