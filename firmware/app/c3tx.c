/**
 * @file c3tx.c
 * @brief Copy MQTT commands to the decision engine without competing workers.
 */
#include "c3tx.h"
#if UTHOME_NETWORK_WIFI
#include "c3bind.h"
#include "c3mqtt.h"
#include "c3uart.h"
#include "txport.h"
#include <string.h>

static txjob_t                 g_job                 = {0};
static txport_t                g_port                = {0};
static const cfgbind_state_t * gp_binding            = NULL;
static const cfgmux_t *        gp_mux                = NULL;
static uint8_t                 g_request[TXJOB_WIRE] = {0};
static uint8_t                 g_reply[TXJOB_WIRE]   = {0};
static bool                    gb_reply              = false;

/** @brief Bind permanent owner storage before restore and audit sealing. */
int c3tx_start (const cfgbind_state_t * p_binding, const cfgmux_t * p_mux)
{
    if ((NULL == p_binding) || (NULL != gp_binding))
    {
        return -1;
    }
    g_port.p_io                  = c3bind_operations();
    g_port.p_tx_empty            = c3uart_tx_empty;
    const txjob_ops_t operations = {.p_begin   = txport_begin,
                                    .p_poll    = txport_poll,
                                    .p_close   = txport_close,
                                    .p_context = &g_port};
    if (0 != txjob_init(&g_job, &operations))
    {
        return -1;
    }
    gp_binding = p_binding;
    gp_mux     = p_mux;
    return 0;
}

/** @brief Cleanup failure keeps this lease until checked release succeeds. */
bool c3tx_busy (void)
{
    return g_job.b_busy;
}

/** @brief Advance I/O and copied replies; never mutate the active node plan. */
void c3tx_step (uint64_t now_ms, uint32_t generation, bool b_online)
{
    if (NULL == gp_binding)
    {
        return;
    }
    if ((!b_online) || (generation != g_job.generation))
    {
        txjob_cancel(&g_job);
    }
    txjob_step(&g_job, now_ms);
    if (!b_online)
    {
        /* Completed request cache survives transport loss; no queued replay. */
        return;
    }
    if ((!gb_reply) && (0 == txjob_take(&g_job, g_reply, sizeof(g_reply))))
    {
        gb_reply = true;
    }
    if (gb_reply)
    {
        if (0 == c3mqtt_result(g_reply, sizeof(g_reply)))
        {
            gb_reply = false;
        }
        return;
    }
    if (!g_job.b_busy)
    {
        size_t size = 0U;
        if (0 == c3mqtt_request(g_request, sizeof(g_request), &size))
        {
            const bool b_idle = (cfgbind_discovery_ready(gp_binding)) &&
                                ((NULL == gp_mux) || (!cfgmux_busy(gp_mux)));
            (void)txjob_submit(
                &g_job, g_request, size, generation, b_idle, now_ms);
            memset(g_request, 0, sizeof(g_request));
        }
    }
}

#endif

/* End of file. */

