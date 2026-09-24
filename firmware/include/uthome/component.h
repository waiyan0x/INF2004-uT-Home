#ifndef UTHOME_COMPONENT_H
#define UTHOME_COMPONENT_H

#include <stddef.h>
#include <stdint.h>


#define UTHOME_COMPONENT_ID_MAX 32


/*
 * Common component result codes.
 */
typedef enum {
    UTHOME_COMPONENT_OK = 0,

    UTHOME_COMPONENT_ERR_INVALID = -1,
    UTHOME_COMPONENT_ERR_UNSUPPORTED = -2,
    UTHOME_COMPONENT_ERR_OPERATION = -3

} uthome_component_result_t;


/*
 * Component operation callbacks.
 *
 * These stay hardware-independent.
 *
 * Individual drivers can later provide
 * implementations through this interface.
 */
typedef struct {

    int (*configure)(
        void *context,
        const void *config,
        size_t config_size
    );

    int (*read)(
        void *context,
        void *data,
        size_t data_capacity
    );

    int (*write)(
        void *context,
        const void *data,
        size_t data_size
    );

    int (*publish)(
        void *context,
        const void *data,
        size_t data_size
    );

} uthome_component_operations_t;


/*
 * Generic component representation.
 */
typedef struct {

    char id[UTHOME_COMPONENT_ID_MAX];

    void *context;

    const uthome_component_operations_t *operations;

} uthome_component_t;


/*
 * Common component API.
 */
int uthome_component_configure(
    uthome_component_t *component,
    const void *config,
    size_t config_size
);


int uthome_component_read(
    uthome_component_t *component,
    void *data,
    size_t data_capacity
);


int uthome_component_write(
    uthome_component_t *component,
    const void *data,
    size_t data_size
);


int uthome_component_publish(
    uthome_component_t *component,
    const void *data,
    size_t data_size
);


#endif