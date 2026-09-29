/**
 * @file bmpinst.c
 * @brief Publish typed samples and explicit faults without device-name runtime
 * logic.
 */
#include "bmpinst.h"

#include <string.h>
#include "shared.h"
#include "workport.h"
#include "uthome/runtime.h"
#include "uthome/sample.h"

static int configure (void * p_context, const void * p_config, size_t size);
static int read_sample (void * p_context, void * p_data, size_t capacity);
static uint16_t sample_status (int result);

static const component_operations_t g_operations = {.p_configure = configure,
                                                    .p_read      = read_sample};

static const drvreg_descriptor_t g_descriptor = {.p_driver_id  = "bmp388",
                                                 .p_operations = &g_operations,
                                                 .context_size =
                                                     sizeof(bmpinst_t)};

/** @brief Expose this compiled implementation for generic registry binding. */
const drvreg_descriptor_t * bmpinst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief Complete one bounded sample attempt, then publish a copied record. */
int bmpinst_step (void * p_context)
{
    bmpinst_t *          p_instance = p_context;
    sample_environment_t sample     = {0};
    const int result = read_sample(p_context, &sample, sizeof(sample));
    if (0 != result)
    {
        return result;
    }
    const int published = shared_publish(p_instance->config.h_destination,
                                         UTHOME_SAMPLE_ENVIRONMENT,
                                         &sample,
                                         sizeof(sample));
    p_instance->last_queue_error = published;
    ++p_instance->sequence;
    /* Contention drops this attempted publication; it never repeats stale data.
     * Queue counters account drop_oldest. A later step takes a fresh sample. */
    return (UTHOME_PORT_ERR_BUSY == published) ? 0 : published;
}

/** @brief Retain a partly configured instance until driver cleanup succeeds. */
int bmpinst_cleanup (void * p_context)
{
    bmpinst_t * p_instance = p_context;
    if (NULL == p_instance)
    {
        return I2CBUS_INVALID;
    }
    const int result = bmp388_close(&p_instance->sensor);
    if (0 == result)
    {
        p_instance->b_configured = false;
    }
    return result;
}

/** @brief Validate scalar settings before configuring a borrowed bus device. */
static int configure (void * p_context, const void * p_config, size_t size)
{
    bmpinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(bmpinst_config_t) != size) || (NULL == p_instance->p_bus) ||
        (p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    bmpinst_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    msgbus_stats_t stats = {0};
    if ((config.source_index >= UTHOME_RUNTIME_MAX_DEVICES) ||
        (0 != shared_queue_stats(config.h_destination, &stats)))
    {
        return I2CBUS_INVALID;
    }
    const int result          = bmp388_open(&p_instance->sensor,
                                   p_instance->p_bus,
                                   config.address,
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

/** @brief Translate driver results to zeroed error records with fresh time. */
static int read_sample (void * p_context, void * p_data, size_t capacity)
{
    bmpinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_data) ||
        (capacity < sizeof(sample_environment_t)) ||
        (!p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    bmp388_value_t value        = {0};
    const int      result       = bmp388_read(&p_instance->sensor, &value);
    p_instance->last_io_error   = result;
    sample_environment_t sample = {0};
    if (0 != i2cbus_now(p_instance->p_bus, &sample.header.timestamp_ms))
    {
        return I2CBUS_IO;
    }
    sample.header.source_index = p_instance->config.source_index;
    sample.header.sequence     = p_instance->sequence;
    sample.header.status       = sample_status(result);
    if (0 == result)
    {
        sample.pressure_pa   = value.pressure_pa;
        sample.temperature_c = value.temperature_c;
    }
    else
    {
        const health_event_t event    = (I2CBUS_TIMEOUT == result)
                                            ? UTHOME_HEALTH_DEVICE_TIMEOUT
                                            : UTHOME_HEALTH_DEVICE_IO;
        const int            reported = shared_report(event);
        if ((reported < 0) && (UTHOME_PORT_ERR_BUSY != reported))
        {
            return reported;
        }
        /* On contention the sample/instance still records the error. The next
         * failing step retries reporting; health counters may undercount. */
    }
    memcpy(p_data, &sample, sizeof(sample));
    return 0;
}

/** @brief Keep bus status codes separate from the stable sample contract. */
static uint16_t sample_status (int result)
{
    switch (result)
    {
    case I2CBUS_OK:
    {
        return UTHOME_SAMPLE_OK;
    }
    case I2CBUS_TIMEOUT:
    {
        return UTHOME_SAMPLE_TIMEOUT;
    }
    case I2CBUS_INVALID:
    {
        return UTHOME_SAMPLE_INVALID;
    }
    default:
    {
        return UTHOME_SAMPLE_IO;
    }
    }
}

/* End of file. */

