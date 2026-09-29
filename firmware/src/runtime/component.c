/**
 * @file component.c
 * @brief Validate and dispatch instance operations through borrowed driver
 * callbacks.
 */
#include "uthome/component.h"

#include <stddef.h>

/**
 * @brief Validate configuration bytes and dispatch the configure callback.
 * @param p_component Component instance containing the borrowed callbacks and
 * context.
 * @param p_config Configuration bytes or queue settings to validate.
 * @param config_size Configuration size in bytes.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int component_configure (component_t * p_component,
                         const void *  p_config,
                         size_t        config_size)
{
    if ((NULL == p_component) || (NULL == p_component->p_operations) ||
        (NULL == p_config) || (0U == config_size))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }

    if (NULL == p_component->p_operations->p_configure)
    {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }

    if (0 != p_component->p_operations->p_configure(
                 p_component->p_context, p_config, config_size))
    {
        return UTHOME_COMPONENT_ERR_OPERATION;
    }

    return UTHOME_COMPONENT_OK;
}

/**
 * @brief Validate destination capacity and dispatch the read callback.
 * @param p_component Component instance containing the borrowed callbacks and
 * context.
 * @param p_data Payload buffer supplied to the selected callback.
 * @param data_capacity Available destination bytes.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int component_read (component_t * p_component,
                    void *        p_data,
                    size_t        data_capacity)
{
    if ((NULL == p_component) || (NULL == p_component->p_operations) ||
        (NULL == p_data) || (0U == data_capacity))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }

    if (NULL == p_component->p_operations->p_read)
    {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }

    if (0 != p_component->p_operations->p_read(
                 p_component->p_context, p_data, data_capacity))
    {
        return UTHOME_COMPONENT_ERR_OPERATION;
    }

    return UTHOME_COMPONENT_OK;
}

/**
 * @brief Validate payload bytes and dispatch the write callback.
 * @param p_component Component instance containing the borrowed callbacks and
 * context.
 * @param p_data Payload buffer supplied to the selected callback.
 * @param data_size Payload size in bytes.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int component_write (component_t * p_component,
                     const void *  p_data,
                     size_t        data_size)
{
    if ((NULL == p_component) || (NULL == p_component->p_operations) ||
        (NULL == p_data) || (0U == data_size))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }

    if (NULL == p_component->p_operations->p_write)
    {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }

    if (0 != p_component->p_operations->p_write(
                 p_component->p_context, p_data, data_size))
    {
        return UTHOME_COMPONENT_ERR_OPERATION;
    }

    return UTHOME_COMPONENT_OK;
}

/**
 * @brief Validate payload bytes and dispatch the publish callback.
 * @param p_component Component instance containing the borrowed callbacks and
 * context.
 * @param p_data Payload buffer supplied to the selected callback.
 * @param data_size Payload size in bytes.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int component_publish (component_t * p_component,
                       const void *  p_data,
                       size_t        data_size)
{
    if ((NULL == p_component) || (NULL == p_component->p_operations) ||
        (NULL == p_data) || (0U == data_size))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }

    if (NULL == p_component->p_operations->p_publish)
    {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }

    if (0 != p_component->p_operations->p_publish(
                 p_component->p_context, p_data, data_size))
    {
        return UTHOME_COMPONENT_ERR_OPERATION;
    }

    return UTHOME_COMPONENT_OK;
}

/* End of file. */

