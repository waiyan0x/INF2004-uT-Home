#include "uthome/component.h"

#include <stddef.h>


int uthome_component_configure(
    uthome_component_t *component,
    const void *config,
    size_t config_size)
{
    if (component == NULL ||
        component->operations == NULL ||
        config == NULL ||
        config_size == 0) {

        return UTHOME_COMPONENT_ERR_INVALID;
    }


    if (component->operations->configure == NULL) {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }


    if (component->operations->configure(
            component->context,
            config,
            config_size
        ) != 0) {

        return UTHOME_COMPONENT_ERR_OPERATION;
    }


    return UTHOME_COMPONENT_OK;
}


int uthome_component_read(
    uthome_component_t *component,
    void *data,
    size_t data_capacity)
{
    if (component == NULL ||
        component->operations == NULL ||
        data == NULL ||
        data_capacity == 0) {

        return UTHOME_COMPONENT_ERR_INVALID;
    }


    if (component->operations->read == NULL) {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }


    if (component->operations->read(
            component->context,
            data,
            data_capacity
        ) != 0) {

        return UTHOME_COMPONENT_ERR_OPERATION;
    }


    return UTHOME_COMPONENT_OK;
}


int uthome_component_write(
    uthome_component_t *component,
    const void *data,
    size_t data_size)
{
    if (component == NULL ||
        component->operations == NULL ||
        data == NULL ||
        data_size == 0) {

        return UTHOME_COMPONENT_ERR_INVALID;
    }


    if (component->operations->write == NULL) {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }


    if (component->operations->write(
            component->context,
            data,
            data_size
        ) != 0) {

        return UTHOME_COMPONENT_ERR_OPERATION;
    }


    return UTHOME_COMPONENT_OK;
}


int uthome_component_publish(
    uthome_component_t *component,
    const void *data,
    size_t data_size)
{
    if (component == NULL ||
        component->operations == NULL ||
        data == NULL ||
        data_size == 0) {

        return UTHOME_COMPONENT_ERR_INVALID;
    }


    if (component->operations->publish == NULL) {
        return UTHOME_COMPONENT_ERR_UNSUPPORTED;
    }


    if (component->operations->publish(
            component->context,
            data,
            data_size
        ) != 0) {

        return UTHOME_COMPONENT_ERR_OPERATION;
    }


    return UTHOME_COMPONENT_OK;
}