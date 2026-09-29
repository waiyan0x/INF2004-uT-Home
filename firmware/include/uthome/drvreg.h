/**
 * @file drvreg.h
 * @brief Bind instances to a bounded table of compiled drivers.
 */
#ifndef UTHOME_DRVREG_H
#define UTHOME_DRVREG_H

#include "uthome/component.h"

#define UTHOME_DRIVER_REGISTRY_MAX 16U

typedef struct
{
    const char *                   p_driver_id;
    const component_operations_t * p_operations;
    size_t                         context_size;
} drvreg_descriptor_t;

typedef struct
{
    const drvreg_descriptor_t * p_entries;
    size_t                      count;
} drvreg_registry_t;

/* Registry p_entries/p_operations must outlive every bound instance. */
int drvreg_init (drvreg_registry_t *         p_registry,
                 const drvreg_descriptor_t * p_entries,
                 size_t                      count);

/* Caller owns distinct, suitably aligned p_context storage for each instance.
 * Binding performs no hardware I/O. Configure only after admission succeeds.
 * A rejected binding leaves the output component unchanged.
 */
int drvreg_bind (const drvreg_registry_t * p_registry,
                 const char *              p_driver_id,
                 const char *              p_instance_id,
                 void *                    p_context,
                 size_t                    context_capacity,
                 component_t *             p_component);

#endif

/* End of file. */

