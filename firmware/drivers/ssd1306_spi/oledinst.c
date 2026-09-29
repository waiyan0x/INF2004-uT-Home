/**
 * @file oledinst.c
 * @brief Preserve display faults for bounded retry without stopping producers.
 */
#include "oledinst.h"
#include <string.h>
#include "shared.h"
#include "workport.h"
static int configure (void * p_context, const void * p_config, size_t size);
static int write_frame (void * p_context, const void * p_data, size_t size);
static const component_operations_t g_operations = {.p_configure = configure,
                                                    .p_write     = write_frame};
static const drvreg_descriptor_t g_descriptor = {.p_driver_id  = "ssd1306_spi",
                                                 .p_operations = &g_operations,
                                                 .context_size =
                                                     sizeof(oledinst_t)};

/** @brief Expose one compiled driver for distinct caller-owned instances. */
const drvreg_descriptor_t * oledinst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief Flush pending pixels; a later step retries after transport failure.
 */
int oledinst_step (void * p_context)
{
    oledinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (!p_instance->display.b_ready))
    {
        return SPIBUS_STATE;
    }
    const int result          = ssd1306_flush(&p_instance->display);
    p_instance->last_io_error = result;
    if (0 != result)
    {
        const health_event_t event    = (SPIBUS_TIMEOUT == result)
                                            ? UTHOME_HEALTH_DEVICE_TIMEOUT
                                            : UTHOME_HEALTH_DEVICE_IO;
        const int            reported = shared_report(event);
        if ((reported < 0) && (UTHOME_PORT_ERR_BUSY != reported))
        {
            return reported;
        }
        /* Busy health accounting may undercount; dirty pixels and the instance
         * error remain. Zero means bounded progress, not a healthy display. */
    }
    return 0;
}

/** @brief Keep the display claim until the off command succeeds. */
int oledinst_cleanup (void * p_context)
{
    oledinst_t * p_instance = p_context;
    return (NULL == p_instance) ? SPIBUS_INVALID
                                : ssd1306_close(&p_instance->display);
}

/** @brief Copy unaligned scalar configuration before opening borrowed hardware.
 */
static int configure (void * p_context, const void * p_config, size_t size)
{
    oledinst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(ssd1306_config_t) != size))
    {
        return SPIBUS_INVALID;
    }
    ssd1306_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    const int result          = ssd1306_open(&p_instance->display,
                                    p_instance->p_spi,
                                    p_instance->data_line,
                                    p_instance->reset,
                                    &config);
    p_instance->last_io_error = result;
    return result;
}

/** @brief Accept copied pixels only; interpretation belongs to the service. */
static int write_frame (void * p_context, const void * p_data, size_t size)
{
    oledinst_t * p_instance = p_context;
    return (NULL == p_instance)
               ? SPIBUS_INVALID
               : ssd1306_write(&p_instance->display, p_data, size);
}

/* End of file. */

