/**
 * @file seninst.c
 * @brief Publish bounded SEN0390 measurements through the existing queue.
 */
#include "seninst.h"

#include <string.h>
#include "shared.h"
#include "workport.h"
#include "uthome/runtime.h"
#include "uthome/sample.h"

static int configure (void * p_context, const void * p_config, size_t size);
static int read_sample (void * p_context, void * p_data, size_t capacity);
static uint16_t sample_status (int result);
static const component_operations_t g_ops        = {.p_configure = configure,
                                                    .p_read      = read_sample};
static const drvreg_descriptor_t    g_descriptor = {.p_driver_id  = "sen0390",
                                                    .p_operations = &g_ops,
                                                    .context_size =
                                                        sizeof(seninst_t)};

/** @brief Expose a compiled adapter, not proof of physical identity. */
const drvreg_descriptor_t * seninst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief Skip publication during EN settling; publish all later attempts. */
int seninst_step (void * p_context)
{
    seninst_t *    p_instance = p_context;
    sample_light_t sample     = {0};
    const int      result     = read_sample(p_context, &sample, sizeof(sample));
    if (UTHOME_SENLUX_WAITING == result)
    {
        return 0;
    }
    if (0 != result)
    {
        return result;
    }
    const int published = shared_publish(p_instance->config.h_destination,
                                         UTHOME_SAMPLE_LIGHT,
                                         &sample,
                                         sizeof(sample));
    p_instance->last_queue_error = published;
    ++p_instance->sequence;
    return (UTHOME_PORT_ERR_BUSY == published) ? 0 : published;
}

/** @brief Keep retained resources until both sensor and line cleanup succeed.
 */
int seninst_cleanup (void * p_context)
{
    seninst_t * p_instance = p_context;
    if (NULL == p_instance)
    {
        return I2CBUS_INVALID;
    }
    const int result = senlux_close(&p_instance->sensor);
    if (0 == result)
    {
        p_instance->b_configured = false;
    }
    return result;
}

/** @brief Validate and copy the owner-provided configuration before EN. */
static int configure (void * p_context, const void * p_config, size_t size)
{
    seninst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(seninst_config_t) != size) || (NULL == p_instance->p_bus) ||
        (NULL == p_instance->p_enable) || (p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    seninst_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    msgbus_stats_t stats = {0};
    if ((config.source_index >= UTHOME_RUNTIME_MAX_DEVICES) ||
        (0 != shared_queue_stats(config.h_destination, &stats)))
    {
        return I2CBUS_INVALID;
    }
    const int result          = senlux_open(&p_instance->sensor,
                                   p_instance->p_bus,
                                   p_instance->p_enable,
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

/** @brief Translate bus failures into fresh, explicitly faulted records. */
static int read_sample (void * p_context, void * p_data, size_t capacity)
{
    seninst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_data) ||
        (capacity < sizeof(sample_light_t)) || (!p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    sample_light_t sample = {0};
    const int      result = senlux_read(&p_instance->sensor, &sample.millilux);
    p_instance->last_io_error = result;
    if (UTHOME_SENLUX_WAITING == result)
    {
        return result;
    }
    if (0 != i2cbus_now(p_instance->p_bus, &sample.header.timestamp_ms))
    {
        return I2CBUS_IO;
    }
    sample.header.source_index = p_instance->config.source_index;
    sample.header.sequence     = p_instance->sequence;
    sample.header.status       = sample_status(result);
    if (0 != result)
    {
        const health_event_t event    = (I2CBUS_TIMEOUT == result)
                                            ? UTHOME_HEALTH_DEVICE_TIMEOUT
                                            : UTHOME_HEALTH_DEVICE_IO;
        const int            reported = shared_report(event);
        if ((reported < 0) && (UTHOME_PORT_ERR_BUSY != reported))
        {
            return reported;
        }
    }
    memcpy(p_data, &sample, sizeof(sample));
    return 0;
}

/** @brief Keep bus result codes separate from stable sample status values. */
static uint16_t sample_status (int result)
{
    if (0 == result)
    {
        return UTHOME_SAMPLE_OK;
    }
    if (I2CBUS_TIMEOUT == result)
    {
        return UTHOME_SAMPLE_TIMEOUT;
    }
    if (I2CBUS_INVALID == result)
    {
        return UTHOME_SAMPLE_INVALID;
    }
    return UTHOME_SAMPLE_IO;
}

/* End of file. */

