/**
 * @file cfgdisc.c
 * @brief Keep detection separate from identity, activation and durable state.
 */
#include "cfgdisc.h"
#include "cfgjson.h"
#include <string.h>
static int validate (void * p_context, const uint8_t * p_data, size_t size);
static int apply (void * p_context);
static int recover (void * p_context);
static void observe_uart (cfgdisc_t *               p_context,
                          const cfgwire_request_t * p_request,
                          uint8_t *                 p_data,
                          size_t *                  p_size);
static bool empty_configuration (const cfgctrl_t * p_control);
static disci2c_result_t admissible (const cfgdisc_t *         p_context,
                                    const cfgwire_request_t * p_request);
static void encode (const cfgdisc_t *             p_context,
                    const cfgwire_request_t *     p_request,
                    const disci2c_result_data_t * p_result,
                    uint8_t *                     p_data,
                    size_t *                      p_size);
static const cfgctrl_ops_t g_operations = {validate, apply, recover};
/* Preset bench_i2c version 0.1.0. No expected model/value lives in firmware.
 * These register reads are reviewed for the selected connection, not universal
 * safe operations on arbitrary I2C equipment. Expand only after review/build.
 */
static const i2cbus_config_t g_connection = {
    .port = 0U, .sda_gpio = 0U, .scl_gpio = 1U, .clock_hz = 100000U};
static const disci2c_read_t  g_reads[2]        = {{0x76U, 0U}, {0x77U, 0U}};
static const uartio_config_t g_uart_connection = {.baud            = 256000U,
                                                  .rx_buffer_bytes = 512U,
                                                  .port            = 1U,
                                                  .tx_gpio         = 21U,
                                                  .rx_gpio         = 20U,
                                                  .data_bits       = 8U,
                                                  .stop_bits       = 1U,
                                                  .parity          = 0U};

/** @brief Bind the existing I2C backend and controller without opening a bus.
 */
int cfgdisc_init (cfgdisc_t *       p_context,
                  cfgctrl_t *       p_control,
                  cfgbind_state_t * p_binding,
                  const uint8_t *   p_build_hash,
                  discuart_wait_t * p_wait)
{
    if ((NULL == p_context) || (NULL == p_control) || (NULL == p_binding) ||
        (NULL == p_build_hash) || (!p_binding->b_initialized) ||
        (NULL == p_binding->p_io) || (NULL == p_wait) ||
        (NULL != p_context->p_control))
    {
        return -1;
    }
    uartio_t * p_port = p_binding->p_io->p_uart();
    if (NULL == p_port)
    {
        return -1;
    }
    p_context->p_control         = p_control;
    p_context->p_binding         = p_binding;
    p_context->probe.p_open      = p_binding->p_io->p_i2c_open;
    p_context->probe.p_close     = p_binding->p_io->p_i2c_close;
    p_context->probe.p_now       = rt_port_now_ms;
    p_context->uart_probe.p_port = p_port;
    p_context->uart_probe.p_now  = rt_port_now_ms;
    p_context->uart_probe.p_wait = p_wait;
    memcpy(p_context->build_hash, p_build_hash, sizeof(p_context->build_hash));
    return 0;
}

/** @brief Retain ordinary controller sequencing, admission and recovery. */
const cfgctrl_ops_t * cfgdisc_operations (void)
{
    return &g_operations;
}

/** @brief Collect raw bytes only after a bounded, versioned preset request. */
int cfgdisc_observe (void *                    p_context,
                     const cfgwire_request_t * p_request,
                     uint8_t *                 p_data,
                     size_t                    capacity,
                     size_t *                  p_size)
{
    cfgdisc_t * p_discovery = p_context;
    if ((NULL == p_discovery) || (NULL == p_discovery->p_control) ||
        (NULL == p_request) || (NULL == p_request->p_data) ||
        (NULL == p_data) || (NULL == p_size) ||
        (CFGWIRE_DISCOVER != p_request->command) ||
        (UTHOME_DISC_REQUEST != p_request->size) ||
        (capacity < (UTHOME_DISC_HEADER + (2U * UTHOME_DISC_RECORD))))
    {
        return -1;
    }
    disci2c_result_data_t result = {.status =
                                        admissible(p_discovery, p_request)};
    if ((DISCI2C_COMPLETE == result.status) &&
        (2U == cfgbytes_get(&p_request->p_data[4])))
    {
        observe_uart(p_discovery, p_request, p_data, p_size);
        return 0;
    }
    if (DISCI2C_COMPLETE == result.status)
    {
        /* Arguments above are fixed and valid; preserve a defensive rejection.
         */
        if (0 != disci2c_run(
                     &p_discovery->probe, &g_connection, g_reads, 2U, &result))
        {
            result.status = DISCI2C_INVALID;
        }
        if (NULL != p_discovery->probe.p_bus)
        {
            /* Only this serialized owner changes the recovery flag. Queries
             * remain available; the wrapped RECOVER retries retained cleanup.
             */
            p_discovery->p_control->b_recovery = true;
        }
    }
    encode(p_discovery, p_request, &result, p_data, p_size);
    return 0;
}

/** @brief Prevent even a valid profile from bypassing retained probe cleanup.
 */
static int validate (void * p_context, const uint8_t * p_data, size_t size)
{
    cfgdisc_t * p_discovery = p_context;
    if ((NULL == p_discovery) || (NULL != p_discovery->probe.p_bus) ||
        (p_discovery->uart_probe.p_port->p_owner == &p_discovery->uart_probe))
    {
        return -1;
    }
    return cfgbind_operations()->p_validate(
        p_discovery->p_binding, p_data, size);
}

/** @brief Delegate only after the same discovery-ownership guard. */
static int apply (void * p_context)
{
    cfgdisc_t * p_discovery = p_context;
    if ((NULL == p_discovery) || (NULL != p_discovery->probe.p_bus) ||
        (p_discovery->uart_probe.p_port->p_owner == &p_discovery->uart_probe))
    {
        return -1;
    }
    return cfgbind_operations()->p_activate(p_discovery->p_binding);
}

/** @brief Explicit recovery retries discovery cleanup before runtime cleanup.
 */
static int recover (void * p_context)
{
    cfgdisc_t * p_discovery = p_context;
    if ((NULL == p_discovery) || (0 != disci2c_release(&p_discovery->probe)) ||
        (0 != discuart_release(&p_discovery->uart_probe)))
    {
        return -1;
    }
    return cfgbind_operations()->p_recover(p_discovery->p_binding);
}

/** @brief Disabled devices and even unused declared buses still reserve wiring.
 */
static bool empty_configuration (const cfgctrl_t * p_control)
{
    if (0U == p_control->active_size)
    {
        return true;
    }
    const cfgjson_value_t * p_root =
        cfgjson_open(p_control->active, p_control->active_size);
    size_t     count = 0U;
    const bool b_empty =
        (NULL != p_root) &&
        (cfgjson_array(cfgjson_get(p_root, "devices"), 0U, &count)) &&
        (cfgjson_array(cfgjson_get(p_root, "interfaces"), 0U, &count));
    cfgjson_close();
    return b_empty;
}

/** @brief No hardware is touched on stale, busy, unconfirmed or unknown input.
 */
static disci2c_result_t admissible (const cfgdisc_t *         p_context,
                                    const cfgwire_request_t * p_request)
{
    const uint8_t * p_data = p_request->p_data;
    if ((1U != cfgbytes_get(&p_data[0])) ||
        ((1U != cfgbytes_get(&p_data[4])) &&
         (2U != cfgbytes_get(&p_data[4]))) ||
        ((0U == cfgbytes_get(&p_data[8])) &&
         (0U == cfgbytes_get(&p_data[12]))) ||
        (1U != cfgbytes_get(&p_data[16])) || (0U != cfgbytes_get(&p_data[20])))
    {
        return DISCI2C_INVALID;
    }
    const cfgctrl_t * p_control = p_context->p_control;
    if (p_request->revision != p_control->active_revision)
    {
        return DISCI2C_STALE;
    }
    size_t workers = 0U;
    if ((!p_control->b_initialized) || (p_control->b_busy) ||
        (p_control->b_recovery) || (p_context->p_binding->b_busy) ||
        (!rt_port_is_owner()) || (0 != rt_port_owned_count(&workers)) ||
        (0U != workers) || (NULL != p_context->p_binding->p_i2c) ||
        (NULL != p_context->p_binding->p_spi) ||
        (NULL != p_context->uart_probe.p_port->p_owner) ||
        (NULL != p_context->probe.p_bus) || (!empty_configuration(p_control)))
    {
        return DISCI2C_BUSY;
    }
    return DISCI2C_COMPLETE;
}

/** @brief Encode explicit little-endian fields; never transmit structure bytes.
 */
static void encode (const cfgdisc_t *             p_context,
                    const cfgwire_request_t *     p_request,
                    const disci2c_result_data_t * p_result,
                    uint8_t *                     p_data,
                    size_t *                      p_size)
{
    const size_t size =
        UTHOME_DISC_HEADER + (p_result->count * UTHOME_DISC_RECORD);
    memset(p_data, 0, size);
    cfgbytes_put(&p_data[0], 1U);
    cfgbytes_put(&p_data[4], p_result->status);
    cfgbytes_put(&p_data[8], cfgbytes_get(&p_request->p_data[4]));
    cfgbytes_put(&p_data[12], p_context->p_control->active_revision);
    memcpy(&p_data[16], &p_request->p_data[8], 8U);
    cfgbytes_put(&p_data[24], (uint32_t)(p_result->started_ms & UINT32_MAX));
    cfgbytes_put(&p_data[28], (uint32_t)(p_result->started_ms >> 32U));
    cfgbytes_put(&p_data[32], p_result->elapsed_ms);
    /* Count is bounded by the fixed read table and the core's eight-record cap.
     */
    cfgbytes_put(&p_data[36], (uint32_t)p_result->count);
    memcpy(&p_data[40], p_context->build_hash, sizeof(p_context->build_hash));
    for (size_t index = 0U; index < p_result->count; ++index)
    {
        uint8_t * p_record =
            &p_data[UTHOME_DISC_HEADER + (index * UTHOME_DISC_RECORD)];
        cfgbytes_put(&p_record[0], p_result->records[index].read.address);
        cfgbytes_put(&p_record[4], p_result->records[index].read.reg);
        cfgbytes_put(&p_record[8], p_result->records[index].status);
        cfgbytes_put(&p_record[12], p_result->records[index].value);
    }
    *p_size = size;
}

/** @brief Encode bounded parser evidence, without promoting it to identity. */
static void observe_uart (cfgdisc_t *               p_context,
                          const cfgwire_request_t * p_request,
                          uint8_t *                 p_data,
                          size_t *                  p_size)
{
    discuart_data_t result = {.status = DISCUART_INVALID};
    if (0 != discuart_run(&p_context->uart_probe, &g_uart_connection, &result))
    {
        result.status = DISCUART_INVALID;
    }
    /* Both observer enums use the bounded discovery wire result codes 0..5. */
    disci2c_result_data_t header = {.status = (disci2c_result_t)result.status,
                                    .started_ms = result.started_ms,
                                    .elapsed_ms = result.elapsed_ms};
    encode(p_context, p_request, &header, p_data, p_size);
    if ((DISCUART_BUSY != result.status) && (DISCUART_INVALID != result.status))
    {
        const uint32_t values[8] = {1U,
                                    result.received_bytes,
                                    result.valid_reports,
                                    result.framing_errors,
                                    result.discarded_bytes,
                                    result.receive_errors,
                                    result.pending_bytes,
                                    result.window_ms};
        cfgbytes_put(&p_data[36], 1U);
        for (size_t index = 0U; index < 8U; ++index)
        {
            cfgbytes_put(&p_data[UTHOME_DISC_HEADER + (index * 4U)],
                         values[index]);
        }
        *p_size += 32U;
    }
    if (p_context->uart_probe.p_port->p_owner == &p_context->uart_probe)
    {
        p_context->p_control->b_recovery = true;
    }
}

/* End of file. */

