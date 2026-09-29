/**
 * @file cmp_main.c
 * @brief Component regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "cmp_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/component.h"

typedef struct
{
    int configured_value;
    int current_value;
    int last_written;
    int last_published;

} fake_component_context_t;

static int
fake_configure (void * p_context, const void * p_config, size_t config_size);
static int fake_read (void * p_context, void * p_data, size_t data_capacity);
static int fake_write (void * p_context, const void * p_data, size_t data_size);
static int
fake_publish (void * p_context, const void * p_data, size_t data_size);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    fake_component_context_t context = {.configured_value = 0,
                                        .current_value    = 25,
                                        .last_written     = 0,
                                        .last_published   = 0};

    const component_operations_t operations = {

        .p_configure = fake_configure,
        .p_read      = fake_read,
        .p_write     = fake_write,
        .p_publish   = fake_publish};

    component_t component = {.p_context    = &context,
                             .p_operations = &operations};

    strncpy(component.name, "test_sensor", sizeof(component.name) - 1U);

    component.name[sizeof(component.name) - 1U] = '\0';

    /*
     * CONFIGURE
     */
    int configuration = 2000;

    assert(UTHOME_COMPONENT_OK == component_configure(&component,
                                                      &configuration,
                                                      sizeof(configuration)));

    assert(2000 == context.configured_value);

    /*
     * READ
     */
    int read_value = 0;

    assert(UTHOME_COMPONENT_OK ==
           component_read(&component, &read_value, sizeof(read_value)));

    assert(25 == read_value);

    /*
     * WRITE
     */
    int write_value = 1;

    assert(UTHOME_COMPONENT_OK ==
           component_write(&component, &write_value, sizeof(write_value)));

    assert(1 == context.last_written);

    /*
     * PUBLISH
     */
    int published_value = 25;

    assert(UTHOME_COMPONENT_OK == component_publish(&component,
                                                    &published_value,
                                                    sizeof(published_value)));

    assert(25 == context.last_published);

    printf("All component operation tests passed!\n");

    return 0;
}

/** @brief Fake configure. */
static int
fake_configure (void * p_context, const void * p_config, size_t config_size)
{
    if (config_size != sizeof(int))
    {
        return -1;
    }

    fake_component_context_t * p_instance = p_context;

    p_instance->configured_value = *(const int *)p_config;

    return 0;
}

/** @brief Fake read. */
static int fake_read (void * p_context, void * p_data, size_t data_capacity)
{
    if (data_capacity < sizeof(int))
    {
        return -1;
    }

    fake_component_context_t * p_instance = p_context;

    *(int *)p_data = p_instance->current_value;

    return 0;
}

/** @brief Fake write. */
static int fake_write (void * p_context, const void * p_data, size_t data_size)
{
    if (data_size != sizeof(int))
    {
        return -1;
    }

    fake_component_context_t * p_instance = p_context;

    p_instance->last_written = *(const int *)p_data;

    return 0;
}

/** @brief Fake publish. */
static int
fake_publish (void * p_context, const void * p_data, size_t data_size)
{
    if (data_size != sizeof(int))
    {
        return -1;
    }

    fake_component_context_t * p_instance = p_context;

    p_instance->last_published = *(const int *)p_data;

    return 0;
}

/* End of file. */

