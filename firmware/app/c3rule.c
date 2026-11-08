/**
 * @file c3rule.c
 * @brief Run the shared transaction model on one native USB owner.
 */
#include "c3rule.h"
#include "c3audit.h"
#include "c3clock.h"
#include "c3i2c.h"
#include "c3usb.h"
#include "txi2c.h"
#include "txwire.h"
#include "workport.h"
#include <tk/tkernel.h>

static txrule_t g_model                        = {0};
static txi2c_t  g_adapter                      = {0};
static uint8_t  g_request[TXWIRE_REQUEST_SIZE] = {0};
static uint8_t  g_reply[TXWIRE_REPLY_SIZE]     = {0};
static int exchange_bytes (bool b_send, uint8_t * p_data, size_t size);

/** @brief Reserve existing runtime resources, then serialize all messages. */
int c3rule_run (void)
{
    const i2cbus_config_t config = {
        .port = 0U, .sda_gpio = 0U, .scl_gpio = 1U, .clock_hz = 100000U};
    if ((E_OK != tk_chg_pri(TSK_SELF, 20)) ||
        (0 != rt_port_clock_set(c3clock_now)) || (0 != rt_port_init()) ||
        (0 != c3i2c_init()) || (0 != c3i2c_open(&config, &g_adapter.p_bus)) ||
        (0 != c3usb_open()) || (0 != c3audit_seal()))
    {
        return -1;
    }
    txrule_init(&g_model);
    for (;;)
    {
        if (0 == exchange_bytes(false, g_request, sizeof(g_request)))
        {
            txwire_process(&g_model,
                           g_request,
                           sizeof(g_request),
                           txi2c_read,
                           &g_adapter,
                           g_reply);
            /* Transport failure discards this reply, never the active rule.
             * No automatic retries; reconnect only after the two-second gap.
             */
            (void)exchange_bytes(true, g_reply, sizeof(g_reply));
        }
    }
}

/** @brief Collect or send one frame with bounded waiting and partial I/O. */
static int exchange_bytes (bool b_send, uint8_t * p_data, size_t size)
{
    const cfgpump_ops_t * p_io  = c3usb_operations();
    uint64_t              start = 0U;
    size_t                used  = 0U;
    if (0 != rt_port_now_ms(&start))
    {
        return -1;
    }
    while (used < size)
    {
        uint64_t now = 0U;
        if ((0 != rt_port_now_ms(&now)) || (now < start) ||
            ((now - start) >= 2000U))
        {
            return -1;
        }
        const size_t remaining = size - used;
        const size_t chunk     = (remaining > 48U) ? 48U : remaining;
        size_t       count     = 0U;
        const int    status =
            b_send ? p_io->p_write(NULL, &p_data[used], chunk, &count)
                      : p_io->p_read(NULL, &p_data[used], chunk, &count);
        if ((0 != status) || (count > chunk))
        {
            return -1;
        }
        /* Start RX expiry at the first byte, not while the host is idle. */
        if ((!b_send) && (0U == used))
        {
            start = now;
        }
        used += count;
        if (E_OK != tk_dly_tsk(1))
        {
            return -1;
        }
    }
    return 0;
}

/* End of file. */

