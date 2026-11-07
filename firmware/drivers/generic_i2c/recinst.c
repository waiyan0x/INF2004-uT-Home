/**
 * @file recinst.c
 * @brief Publish selected raw bytes without interpreting physical units.
 */
#include "recinst.h"

#include <string.h>
#include "shared.h"
#include "workport.h"
#include "uthome/runtime.h"

static int configure (void * p_context, const void * p_config, size_t size);
static int read_sample (void * p_context, void * p_data, size_t capacity);
static uint16_t sample_status (int result);
static const component_operations_t g_ops        = {.p_configure = configure,
                                                    .p_read      = read_sample};
static const drvreg_descriptor_t    g_descriptor = {.p_driver_id = "i2c_recipe",
                                                    .p_operations = &g_ops,
                                                    .context_size =
                                                        sizeof(recinst_t)};

/** @brief Expose one fixed engine for admitted register-read settings. */
const drvreg_descriptor_t * recinst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief Complete one bounded transaction and publish its copied outcome. */
int recinst_step (void * p_context)
{
    recinst_t *  p_instance = p_context;
    sample_raw_t sample     = {0};
    const int    result     = read_sample(p_context, &sample, sizeof(sample));
    if (0 != result)
    {
        return result;
    }
    const int published = shared_publish(p_instance->config.h_destination,
                                         UTHOME_SAMPLE_RAW,
                                         &sample,
                                         sizeof(sample));
    p_instance->last_queue_error = published;
    ++p_instance->sequence;
    return (UTHOME_PORT_ERR_BUSY == published) ? 0 : published;
}

/** @brief Release the address only after its worker has stopped. */
int recinst_cleanup (void * p_context)
{
    recinst_t * p_instance = p_context;
    if (NULL == p_instance)
    {
        return I2CBUS_INVALID;
    }
    const int result = geni2c_close(&p_instance->sensor);
    if (0 == result)
    {
        p_instance->b_configured = false;
    }
    return result;
}

/** @brief Recheck fixed bounds before claiming a borrowed I2C address. */
static int configure (void * p_context, const void * p_config, size_t size)
{
    recinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(recinst_config_t) != size) || (NULL == p_instance->p_bus) ||
        (p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    recinst_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    msgbus_stats_t stats = {0};
    if ((config.source_index >= UTHOME_RUNTIME_MAX_DEVICES) ||
        (config.recipe.address < 0x08U) || (config.recipe.address > 0x77U) ||
        (0U == config.recipe.read_size) ||
        (config.recipe.read_size > UTHOME_SAMPLE_RAW_MAX) ||
        (config.select_offset >= config.recipe.read_size) ||
        (0U == config.select_length) ||
        ((config.select_offset + config.select_length) >
         config.recipe.read_size) ||
        (config.timeout_ms < 10U) || (config.timeout_ms > 100U) ||
        (0 != shared_queue_stats(config.h_destination, &stats)))
    {
        return I2CBUS_INVALID;
    }
    const int result          = geni2c_open(&p_instance->sensor,
                                   p_instance->p_bus,
                                   &config.recipe,
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

/** @brief Retain only successful selected bytes; faults carry no old value. */
static int read_sample (void * p_context, void * p_data, size_t capacity)
{
    recinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_data) ||
        (capacity < sizeof(sample_raw_t)) || (!p_instance->b_configured))
    {
        return I2CBUS_INVALID;
    }
    uint8_t      bytes[UTHOME_SAMPLE_RAW_MAX] = {0U};
    sample_raw_t sample                       = {0};
    const int result = geni2c_read(&p_instance->sensor, bytes, sizeof(bytes));
    p_instance->last_io_error = result;
    if (0 != i2cbus_now(p_instance->p_bus, &sample.header.timestamp_ms))
    {
        return I2CBUS_IO;
    }
    sample.header.source_index = p_instance->config.source_index;
    sample.header.sequence     = p_instance->sequence;
    sample.header.status       = sample_status(result);
    if (0 == result)
    {
        sample.length = p_instance->config.select_length;
        memcpy(sample.bytes,
               &bytes[p_instance->config.select_offset],
               sample.length);
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
    }
    memcpy(p_data, &sample, sizeof(sample));
    return 0;
}

/** @brief Preserve the distinction between transport and sample status. */
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
    return (I2CBUS_INVALID == result) ? UTHOME_SAMPLE_INVALID
                                      : UTHOME_SAMPLE_IO;
}

/* End of file. */

