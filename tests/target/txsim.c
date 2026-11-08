/**
 * @file txsim.c
 * @brief Simulated peripheral with real rule, recipe and bus implementations.
 */
#include "txsim.h"
#include "txi2c.h"
#include <string.h>

static int transfer (void * p_context, const i2cbus_request_t * p_request);
static int now (void * p_context, uint64_t * p_now);
static int delay (void * p_context, uint32_t duration_us, uint64_t deadline_ms);
static const i2cbus_ops_t g_operations = {
    .p_transfer = transfer, .p_now = now, .p_delay = delay};
static txrule_t g_state       = {0};
static i2cbus_t g_bus         = {0};
static txi2c_t  g_adapter     = {0};
static uint32_t g_calls       = 0U;
static uint32_t g_observed[4] = {0U};
static int      g_fault       = 0;

/** @brief Start a fresh host test; no physical bus exists. */
void txsim_init (void)
{
    txrule_init(&g_state);
    g_bus = (i2cbus_t){.p_operations = &g_operations, .p_context = &g_calls};
    g_adapter = (txi2c_t){.p_bus = &g_bus};
    g_calls   = 0U;
    memset(g_observed, 0, sizeof(g_observed));
    g_fault = 0;
}

void txsim_exchange (const uint8_t * p_input, size_t size, uint8_t * p_output)
{
    txwire_process(&g_state, p_input, size, txi2c_read, &g_adapter, p_output);
}

void txsim_fault (int status)
{
    g_fault = status;
}

uint32_t txsim_calls (void)
{
    return g_calls;
}

uint32_t txsim_observed (uint32_t field)
{
    return (4U > field) ? g_observed[field] : 0U;
}

size_t txsim_size (uint32_t field)
{
    return (0U == field)
               ? sizeof(txrule_config_t)
               : ((1U == field) ? sizeof(txrule_t) : sizeof(txrule_result_t));
}

/** @brief Fill synthetic bytes; even an error writes junk to test clearing. */
static int transfer (void * p_context, const i2cbus_request_t * p_request)
{
    (void)p_context; /* The fake bus requires no context. */
    ++g_calls;
    if ((1U != p_request->write_size) || (NULL == p_request->p_write) ||
        (NULL == p_request->p_read) || (16U < p_request->read_size) ||
        (150U != p_request->deadline_ms) || (0U != p_request->read_pause_ms))
    {
        return I2CBUS_INVALID;
    }
    g_observed[0] = p_request->address;
    g_observed[1] = p_request->p_write[0];
    g_observed[2] = (uint32_t)p_request->write_size;
    g_observed[3] = (uint32_t)p_request->read_size;
    for (size_t idx = 0U; idx < p_request->read_size; ++idx)
    {
        /* These bytes are synthetic, including a valid zero in every read. */
        p_request->p_read[idx] = (uint8_t)idx;
    }
    return g_fault;
}

static int now (void * p_context, uint64_t * p_now)
{
    (void)p_context; /* Fixed deterministic test clock. */
    *p_now = 100U;
    return I2CBUS_OK;
}

static int delay (void * p_context, uint32_t duration_us, uint64_t deadline_ms)
{
    (void)p_context; /* No pause is permitted in this experiment. */
    (void)duration_us;
    (void)deadline_ms;
    return I2CBUS_UNSUPPORTED;
}

/* End of file. */

