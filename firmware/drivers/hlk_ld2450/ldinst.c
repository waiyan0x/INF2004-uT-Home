/**
 * @file ldinst.c
 * @brief Publish fresh typed reports with transport and framing diagnostics.
 */
#include "ldinst.h"
#include <string.h>
#include "shared.h"
#include "workport.h"
#include "uthome/runtime.h"
static int configure (void * p_context, const void * p_config, size_t size);
static int read_sample (void * p_context, void * p_data, size_t capacity);
static int report_fault (health_event_t event);
static const component_operations_t g_operations = {.p_configure = configure,
                                                    .p_read      = read_sample};

static const drvreg_descriptor_t g_descriptor = {.p_driver_id  = "hlk_ld2450",
                                                 .p_operations = &g_operations,
                                                 .context_size =
                                                     sizeof(ldinst_t)};

/** @brief Binding only describes an implementation and performs no I/O. */
const drvreg_descriptor_t * ldinst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief A missing report is normal; never republish the preceding sample. */
int ldinst_step (void * p_context)
{
    ldinst_t *             p_instance = p_context;
    sample_target_report_t sample     = {0};
    const int result = read_sample(p_context, &sample, sizeof(sample));
    if (UARTIO_AGAIN == result)
    {
        return 0;
    }
    if (0 != result)
    {
        return result;
    }
    const int published = shared_publish(p_instance->config.h_destination,
                                         UTHOME_SAMPLE_TARGETS,
                                         &sample,
                                         sizeof(sample));
    p_instance->last_queue_error = published;
    ++p_instance->sequence;
    return (UTHOME_PORT_ERR_BUSY == published) ? 0 : published;
}

/** @brief Keep partially opened resources until driver cleanup succeeds. */
int ldinst_cleanup (void * p_context)
{
    ldinst_t * p_instance = p_context;
    if (NULL == p_instance)
    {
        return UARTIO_INVALID;
    }
    const int result = ld2450_close(&p_instance->sensor);
    if (0 == result)
    {
        p_instance->b_configured = false;
    }
    return result;
}

/** @brief Check the copied scalar contract before claiming the UART. */
static int configure (void * p_context, const void * p_config, size_t size)
{
    ldinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(ldinst_config_t) != size) || (NULL == p_instance->p_port) ||
        (p_instance->b_configured))
    {
        return UARTIO_INVALID;
    }
    ldinst_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    msgbus_stats_t stats = {0};
    if ((config.source_index >= UTHOME_RUNTIME_MAX_DEVICES) ||
        (0 != shared_queue_stats(config.h_destination, &stats)))
    {
        return UARTIO_INVALID;
    }
    const int result          = ld2450_open(&p_instance->sensor,
                                   p_instance->p_port,
                                   &config.uart,
                                   config.timeout_ms);
    p_instance->last_io_error = result;
    if (0 == result)
    {
        p_instance->config           = config;
        p_instance->sequence         = 0U;
        p_instance->last_queue_error = 0;
        p_instance->b_configured     = true;
    }
    return result;
}

/** @brief Count bad-frame episodes even if that batch also contains a report.
 */
static int read_sample (void * p_context, void * p_data, size_t capacity)
{
    ldinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_data) ||
        (capacity < sizeof(sample_target_report_t)) ||
        (!p_instance->b_configured))
    {
        return UARTIO_INVALID;
    }
    sample_target_report_t sample = {0};
    const uint32_t         errors = p_instance->sensor.parser.framing_errors;
    const int              result = ld2450_poll(&p_instance->sensor, &sample);
    p_instance->last_io_error     = result;
    const bool b_frame_fault =
        (errors != p_instance->sensor.parser.framing_errors) ||
        (UARTIO_FRAME == result);
    if (b_frame_fault && (0 != report_fault(UTHOME_HEALTH_UART_FRAME_INVALID)))
    {
        return UARTIO_IO;
    }
    if ((UARTIO_AGAIN == result) || (UARTIO_STATE == result) ||
        (UARTIO_INVALID == result))
    {
        return result;
    }
    sample.header.source_index = p_instance->config.source_index;
    sample.header.sequence     = p_instance->sequence;
    if (result < 0)
    {
        sample.header.status = (UARTIO_TIMEOUT == result)
                                   ? UTHOME_SAMPLE_TIMEOUT
                                   : UTHOME_SAMPLE_IO;
        if (UARTIO_FRAME != result)
        {
            const health_event_t event =
                (UARTIO_TIMEOUT == result)
                    ? UTHOME_HEALTH_DEVICE_TIMEOUT
                    : ((UARTIO_OVERFLOW == result)
                           ? UTHOME_HEALTH_UART_RX_OVERFLOW
                           : UTHOME_HEALTH_DEVICE_IO);
            if (0 != report_fault(event))
            {
                return UARTIO_IO;
            }
        }
    }
    memcpy(p_data, &sample, sizeof(sample));
    return 0;
}

/** @brief Health contention drops only the counter update, never spins. */
static int report_fault (health_event_t event)
{
    const int result = shared_report(event);
    return ((result >= 0) || (UTHOME_PORT_ERR_BUSY == result)) ? 0 : result;
}

/* End of file. */

