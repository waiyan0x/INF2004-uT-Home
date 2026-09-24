#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/component.h"


typedef struct {
    int configured_value;
    int current_value;
    int last_written;
    int last_published;

} fake_component_context_t;


static int fake_configure(
    void *context,
    const void *config,
    size_t config_size)
{
    if (config_size != sizeof(int)) {
        return -1;
    }

    fake_component_context_t *ctx =
        context;

    ctx->configured_value =
        *(const int *)config;

    return 0;
}


static int fake_read(
    void *context,
    void *data,
    size_t data_capacity)
{
    if (data_capacity < sizeof(int)) {
        return -1;
    }

    fake_component_context_t *ctx =
        context;

    *(int *)data =
        ctx->current_value;

    return 0;
}


static int fake_write(
    void *context,
    const void *data,
    size_t data_size)
{
    if (data_size != sizeof(int)) {
        return -1;
    }

    fake_component_context_t *ctx =
        context;

    ctx->last_written =
        *(const int *)data;

    return 0;
}


static int fake_publish(
    void *context,
    const void *data,
    size_t data_size)
{
    if (data_size != sizeof(int)) {
        return -1;
    }

    fake_component_context_t *ctx =
        context;

    ctx->last_published =
        *(const int *)data;

    return 0;
}


int main(void)
{
    fake_component_context_t context = {
        .configured_value = 0,
        .current_value = 25,
        .last_written = 0,
        .last_published = 0
    };


    const uthome_component_operations_t
        operations = {

            .configure = fake_configure,
            .read = fake_read,
            .write = fake_write,
            .publish = fake_publish
        };


    uthome_component_t component = {
        .context = &context,
        .operations = &operations
    };


    strncpy(
        component.id,
        "test_sensor",
        sizeof(component.id) - 1
    );

    component.id[
        sizeof(component.id) - 1
    ] = '\0';


    /*
     * CONFIGURE
     */
    int configuration = 2000;

    assert(
        uthome_component_configure(
            &component,
            &configuration,
            sizeof(configuration)
        ) == UTHOME_COMPONENT_OK
    );

    assert(
        context.configured_value == 2000
    );


    /*
     * READ
     */
    int read_value = 0;

    assert(
        uthome_component_read(
            &component,
            &read_value,
            sizeof(read_value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(read_value == 25);


    /*
     * WRITE
     */
    int write_value = 1;

    assert(
        uthome_component_write(
            &component,
            &write_value,
            sizeof(write_value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(context.last_written == 1);


    /*
     * PUBLISH
     */
    int published_value = 25;

    assert(
        uthome_component_publish(
            &component,
            &published_value,
            sizeof(published_value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(
        context.last_published == 25
    );


    printf(
        "All component operation tests passed!\n"
    );

    return 0;
}