/**
 * @file drvreg.c
 * @brief Validate driver descriptors and bind independent component instances.
 */
#include "uthome/drvreg.h"
#include <stdbool.h>
#include <string.h>

static bool is_valid_name (const char * p_name);

/**
 * @brief Validate descriptor names and ownership before accepting a registry.
 * @param p_registry Driver registry whose borrowed descriptors remain
 * caller-owned.
 * @param p_entries Borrowed descriptor array; NULL is permitted for an empty
 * registry.
 * @param count Number of descriptors in the array.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int drvreg_init (drvreg_registry_t *         p_registry,
                 const drvreg_descriptor_t * p_entries,
                 size_t                      count)
{
    if ((NULL == p_registry) || (count > UTHOME_DRIVER_REGISTRY_MAX) ||
        ((count > 0U) && (NULL == p_entries)))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        if ((!is_valid_name(p_entries[index].p_driver_id)) ||
            (NULL == p_entries[index].p_operations) ||
            (0U == p_entries[index].context_size))
        {
            return UTHOME_COMPONENT_ERR_INVALID;
        }
        for (size_t previous = 0U; previous < index; ++previous)
        {
            if (0 == strcmp(p_entries[index].p_driver_id,
                            p_entries[previous].p_driver_id))
            {
                return UTHOME_COMPONENT_ERR_INVALID;
            }
        }
    }
    *p_registry = (drvreg_registry_t){.p_entries = p_entries, .count = count};
    return UTHOME_COMPONENT_OK;
}

/**
 * @brief Bind a supported driver to an independent caller-owned context.
 * @param p_registry Driver registry whose borrowed descriptors remain
 * caller-owned.
 * @param p_driver_id Registered driver name to select.
 * @param p_instance_id Unique caller-selected component instance name.
 * @param p_context Caller-owned context storage for this component instance.
 * @param context_capacity Available context storage in bytes.
 * @param p_component Component instance containing the borrowed callbacks and
 * context.
 * @return Component success or the validation, unsupported-operation, or
 * callback error.
 */
int drvreg_bind (const drvreg_registry_t * p_registry,
                 const char *              p_driver_id,
                 const char *              p_instance_id,
                 void *                    p_context,
                 size_t                    context_capacity,
                 component_t *             p_component)
{
    if ((NULL == p_registry) || (!is_valid_name(p_driver_id)) ||
        (!is_valid_name(p_instance_id)) || (NULL == p_context) ||
        (NULL == p_component) ||
        (p_registry->count > UTHOME_DRIVER_REGISTRY_MAX) ||
        ((p_registry->count > 0U) && (NULL == p_registry->p_entries)))
    {
        return UTHOME_COMPONENT_ERR_INVALID;
    }
    for (size_t index = 0U; index < p_registry->count; ++index)
    {
        const drvreg_descriptor_t * p_entry = &p_registry->p_entries[index];
        if (0 != strcmp(p_entry->p_driver_id, p_driver_id))
        {
            continue;
        }
        if (context_capacity < p_entry->context_size)
        {
            return UTHOME_COMPONENT_ERR_INVALID;
        }
        component_t candidate = {.p_context    = p_context,
                                 .p_operations = p_entry->p_operations};
        memcpy(candidate.name, p_instance_id, strlen(p_instance_id) + 1U);
        *p_component = candidate;
        return UTHOME_COMPONENT_OK;
    }
    return UTHOME_COMPONENT_ERR_UNSUPPORTED;
}

/**
 * @brief Check for a nonempty name terminated within the component name
 * capacity.
 * @param p_name Name to check without reading beyond its documented capacity.
 * @return true if valid, otherwise false.
 */
static bool is_valid_name (const char * p_name)
{
    if ((NULL == p_name) || ('\0' == p_name[0]))
    {
        return false;
    }
    for (size_t index = 0U; index < UTHOME_COMPONENT_ID_MAX; ++index)
    {
        if ('\0' == p_name[index])
        {
            return true;
        }
    }
    return false;
}

/* End of file. */

