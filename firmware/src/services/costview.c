/**
 * @file costview.c
 * @brief Serialize explicit identities and availability without raw structs.
 */
#include "costview.h"
#include <string.h>

static void
encode_row (const costview_t * p_state, size_t index, uint8_t * p_data);
static void
encode_stack (costview_stack_fn_t * p_probe, uint32_t bytes, uint8_t * p_data);
static void encode_cpu (costview_cpu_fn_t * p_probe, uint8_t * p_data);

/** @brief Copy coherent costs, then sample bounded controller-owned metadata.
 */
int costview_observe (void *    p_context,
                      uint8_t * p_data,
                      size_t    capacity,
                      size_t *  p_size)
{
    costview_t * p_state = p_context;
    if ((NULL == p_state) || (NULL == p_state->p_control) ||
        (!p_state->p_control->b_initialized) || (NULL == p_data) ||
        (NULL == p_size) || (capacity < COSTVIEW_BYTES) ||
        (!rt_port_is_owner()))
    {
        return -1;
    }
    memset(p_data, 0, COSTVIEW_BYTES);
    const uint32_t version = (NULL == p_state->p_cpu) ? 1U : 2U;
    cfgbytes_put(p_data, version);
    *p_size          = COSTVIEW_HEADER;
    uint64_t started = 0U;
    int      result  = -1;
    if ((NULL != p_state->p_clock) && (0 == p_state->p_clock(&started)) &&
        (0 == activate_get_status(&p_state->runtime)) &&
        (0 == rt_port_stack_capture(p_state->stacks,
                                    UTHOME_RUNTIME_MAX_DEVICE_TASKS)))
    {
        result = rt_port_cost_capture(p_state->costs,
                                      UTHOME_RUNTIME_MAX_DEVICE_TASKS);
    }
    if (0 != result)
    {
        cfgbytes_put(&p_data[4], (UTHOME_PORT_ERR_BUSY == result) ? 1U : 2U);
        return 0;
    }
    cfgbytes_put(&p_data[8], p_state->runtime.generation);
    cfgbytes_put(&p_data[12], p_state->p_control->active_revision);
    cfgbytes_put(&p_data[16], p_state->p_control->active_crc);
    cfgbytes_put(&p_data[20], UTHOME_RUNTIME_MAX_DEVICE_TASKS);
    /* Explicit low/high words preserve the complete unsigned clock. */
    cfgbytes_put(&p_data[24], (uint32_t)(started & UINT32_MAX));
    cfgbytes_put(&p_data[28], (uint32_t)(started >> 32U));
    encode_stack(p_state->p_controller, 8448U, &p_data[32]);
    encode_stack(p_state->p_idle, 1024U, &p_data[44]);
    encode_cpu(p_state->p_cpu, &p_data[60]);
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        encode_row(
            p_state, index, &p_data[COSTVIEW_HEADER + index * COSTVIEW_ROW]);
    }
    uint64_t finished = 0U;
    if ((0 != p_state->p_clock(&finished)) || (finished < started) ||
        ((finished - started) > UINT32_MAX))
    {
        memset(p_data, 0, COSTVIEW_BYTES);
        cfgbytes_put(p_data, version);
        cfgbytes_put(&p_data[4], 2U);
        return 0;
    }
    /* Duration was checked before narrowing; includes stack scans/encoding. */
    cfgbytes_put(&p_data[56], (uint32_t)(finished - started));
    *p_size = COSTVIEW_BYTES;
    return 0;
}

/** @brief Encode a coherent tick observation without synthesizing CPU usage. */
static void encode_cpu (costview_cpu_fn_t * p_probe, uint8_t * p_data)
{
    costview_cpu_t sample = {0};
    if ((NULL != p_probe) && (0 == p_probe(&sample)) && (sample.tick_ms > 0U) &&
        (sample.tick_ms <= 1000U))
    {
        cfgbytes_put(p_data, 1U);
        cfgbytes_put(&p_data[4], (uint32_t)(sample.elapsed_ms & UINT32_MAX));
        cfgbytes_put(&p_data[8], (uint32_t)(sample.elapsed_ms >> 32U));
        cfgbytes_put(&p_data[12], sample.idle_ms);
        cfgbytes_put(&p_data[16], sample.tick_ms);
    }
}

/** @brief Never encode an unavailable stack as a measured zero. */
static void
encode_stack (costview_stack_fn_t * p_probe, uint32_t bytes, uint8_t * p_data)
{
    uint32_t unused = 0U;
    cfgbytes_put(p_data, bytes);
    if ((NULL != p_probe) && (0 == p_probe(&unused)) && (unused <= bytes))
    {
        cfgbytes_put(&p_data[4], unused);
        cfgbytes_put(&p_data[8], 1U);
    }
}

/** @brief Encode one physical permanent slot, including unbound stack history.
 */
static void
encode_row (const costview_t * p_state, size_t index, uint8_t * p_data)
{
    const rt_port_cost_t *  p_cost  = &p_state->costs[index];
    const rt_port_stack_t * p_stack = &p_state->stacks[index];
    /* The loop bounds the physical slot index to zero through three. */
    cfgbytes_put(p_data, (uint32_t)index);
    uint32_t flags = p_stack->b_measured ? 4U : 0U;
    if (p_cost->b_bound)
    {
        flags |= 1U;
        cfgbytes_put(&p_data[4], p_cost->source_index);
        /* Bound handles are strictly positive signed 32-bit values. */
        cfgbytes_put(&p_data[8], (uint32_t)p_cost->h_task);
        if (p_cost->b_available)
        {
            flags |= 2U;
            const workcost_stats_t * p_stats = &p_cost->stats;
            cfgbytes_put(&p_data[24], p_stats->steps);
            cfgbytes_put(&p_data[28], p_stats->elapsed_us);
            cfgbytes_put(&p_data[32], p_stats->max_elapsed_us);
            cfgbytes_put(&p_data[36], p_stats->max_lateness_us);
            cfgbytes_put(&p_data[40], p_stats->bus_wait_us);
            cfgbytes_put(&p_data[44], p_stats->bus_active_us);
            cfgbytes_put(&p_data[48], p_stats->transfer_bytes);
            cfgbytes_put(&p_data[52], p_stats->transfers);
            cfgbytes_put(&p_data[56], p_stats->timeouts);
            cfgbytes_put(&p_data[60], p_stats->errors);
            cfgbytes_put(&p_data[64], p_stats->flags);
        }
    }
    cfgbytes_put(&p_data[12], flags);
    cfgbytes_put(&p_data[16], UTHOME_PORT_WORKER_STACK_BYTES);
    if (p_stack->b_measured)
    {
        cfgbytes_put(&p_data[20], p_stack->unused_min);
    }
}

/* End of file. */

