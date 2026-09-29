/**
 * @file component.h
 * @brief Dispatch a bound component through explicit operation callbacks.
 */
#ifndef UTHOME_COMPONENT_H
#define UTHOME_COMPONENT_H

#include <stddef.h>
#include <stdint.h>

#define UTHOME_COMPONENT_ID_MAX 32U

/*
 * Common component result codes.
 */
typedef enum
{
    UTHOME_COMPONENT_OK = 0,

    UTHOME_COMPONENT_ERR_INVALID     = -1,
    UTHOME_COMPONENT_ERR_UNSUPPORTED = -2,
    UTHOME_COMPONENT_ERR_OPERATION   = -3

} component_result_t;

/** @brief Configure a component through its driver-owned context. */
typedef int component_configure_fn_t (void *       p_context,
                                      const void * p_config,
                                      size_t       config_size);

/** @brief Read a component value into caller-owned storage. */
typedef int
component_read_fn_t (void * p_context, void * p_data, size_t data_capacity);

/** @brief Write a component value from caller-owned storage. */
typedef int
component_write_fn_t (void * p_context, const void * p_data, size_t data_size);

/** @brief Publish a component value from caller-owned storage. */
typedef int component_publish_fn_t (void *       p_context,
                                    const void * p_data,
                                    size_t       data_size);

/*
 * Component operation callbacks.
 *
 * These stay hardware-independent.
 *
 * Individual drivers can later provide
 * implementations through this interface.
 */
typedef struct
{
    component_configure_fn_t * p_configure;

    component_read_fn_t * p_read;

    component_write_fn_t * p_write;

    component_publish_fn_t * p_publish;

} component_operations_t;

/*
 * Generic component representation.
 */
typedef struct
{
    char name[UTHOME_COMPONENT_ID_MAX];

    void * p_context;

    const component_operations_t * p_operations;

} component_t;

/*
 * Common component API.
 */
int component_configure (component_t * p_component,
                         const void *  p_config,
                         size_t        config_size);

int component_read (component_t * p_component,
                    void *        p_data,
                    size_t        data_capacity);

int component_write (component_t * p_component,
                     const void *  p_data,
                     size_t        data_size);

int component_publish (component_t * p_component,
                       const void *  p_data,
                       size_t        data_size);

#endif

/* End of file. */

