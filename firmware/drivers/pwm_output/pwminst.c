/**
 * @file pwminst.c
 * @brief Keep component dispatch independent of a particular actuator.
 */
#include "pwminst.h"

#include <string.h>
static int configure (void * p_context, const void * p_config, size_t size);
static int write_command (void * p_context, const void * p_data, size_t size);
static const component_operations_t g_operations = {.p_configure = configure,
                                                    .p_write = write_command};

static const drvreg_descriptor_t g_descriptor = {.p_driver_id  = "pwm_output",
                                                 .p_operations = &g_operations,
                                                 .context_size =
                                                     sizeof(pwminst_t)};

/** @brief Registry binding only borrows the caller-owned instance. */
const drvreg_descriptor_t * pwminst_descriptor (void)
{
    return &g_descriptor;
}

/** @brief Stop output before releasing its channel, including failed setup. */
int pwminst_cleanup (void * p_context)
{
    pwminst_t * p_instance = p_context;
    if (NULL == p_instance)
    {
        return PWMIO_INVALID;
    }
    p_instance->b_configured = false;
    if ((NULL != p_instance->p_port) &&
        (p_instance == p_instance->p_port->p_owner))
    {
        return pwmio_close(p_instance->p_port, p_instance);
    }
    return 0;
}

/** @brief Copy possibly unaligned configuration before setting up zero duty. */
static int configure (void * p_context, const void * p_config, size_t size)
{
    pwminst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_config) ||
        (sizeof(pwmio_config_t) != size) || (p_instance->b_configured))
    {
        return PWMIO_INVALID;
    }
    pwmio_config_t config = {0};
    memcpy(&config, p_config, sizeof(config));
    const int result = pwmio_open(p_instance->p_port, &config, p_instance);
    p_instance->b_configured = (0 == result);
    return result;
}

/** @brief Exact command size and transport bounds reject ambiguous writes. */
static int write_command (void * p_context, const void * p_data, size_t size)
{
    pwminst_t * p_instance = p_context;
    if ((NULL == p_instance) || (NULL == p_data) ||
        (sizeof(pwminst_command_t) != size) || (!p_instance->b_configured))
    {
        return PWMIO_INVALID;
    }
    pwminst_command_t command = {0};
    memcpy(&command, p_data, sizeof(command));
    return pwmio_set(p_instance->p_port, p_instance, command.duty_permille);
}

/* End of file. */

