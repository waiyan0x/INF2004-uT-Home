/**
 * @file reg_main.c
 * @brief Driver registry regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "reg_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uthome/drvreg.h"

typedef struct
{
    int value;
} counter_context_t;

static int read_counter (void * p_context, void * p_data, size_t capacity);
static void verify_selected (const drvreg_descriptor_t * p_descriptor);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    const component_operations_t operations = {.p_read = read_counter};
    const drvreg_descriptor_t    drivers[]  = {
        {"test_counter", &operations, sizeof(counter_context_t)},
        {"additional_counter", &operations, sizeof(counter_context_t)}};
    drvreg_registry_t registry = {0};
    assert(0 == drvreg_init(&registry, drivers, 2));
    counter_context_t contexts[]   = {{0}, {10}, {20}};
    component_t       instances[3] = {0};
    assert(0 == drvreg_bind(&registry,
                            "test_counter",
                            "room_a",
                            &contexts[0],
                            sizeof(contexts[0]),
                            &instances[0]));
    assert(0 == drvreg_bind(&registry,
                            "test_counter",
                            "room_b",
                            &contexts[1],
                            sizeof(contexts[1]),
                            &instances[1]));
    assert(0 == drvreg_bind(&registry,
                            "additional_counter",
                            "room_c",
                            &contexts[2],
                            sizeof(contexts[2]),
                            &instances[2]));
    int value = 0;
    assert((0 == component_read(&instances[0], &value, sizeof(value))) &&
           (1 == value));
    assert((0 == component_read(&instances[1], &value, sizeof(value))) &&
           (11 == value));
    assert((0 == component_read(&instances[0], &value, sizeof(value))) &&
           (2 == value));
    assert((0 == component_read(&instances[2], &value, sizeof(value))) &&
           (21 == value));
    assert(UTHOME_COMPONENT_ERR_UNSUPPORTED ==
           component_write(&instances[0], &value, sizeof(value)));
    component_t original = instances[0];
    assert(UTHOME_COMPONENT_ERR_UNSUPPORTED == drvreg_bind(&registry,
                                                           "missing",
                                                           "room_a",
                                                           &contexts[0],
                                                           sizeof(contexts[0]),
                                                           &instances[0]));
    assert(0 == memcmp(&original, &instances[0], sizeof(original)));
    assert(UTHOME_COMPONENT_ERR_INVALID == drvreg_bind(&registry,
                                                       "test_counter",
                                                       "room_a",
                                                       &contexts[0],
                                                       0,
                                                       &instances[0]));
    const drvreg_descriptor_t duplicates[] = {drivers[0], drivers[0]};
    assert(UTHOME_COMPONENT_ERR_INVALID ==
           drvreg_init(&registry, duplicates, 2));
    assert((registry.p_entries == drivers) && (2 == registry.count));
    verify_selected(&drivers[0]);
    puts("Driver registry: independent instances, extension and rejection "
         "passed.");
    return 0;
}

/** @brief Selected descriptors retain validation and leave rejected output. */
static void verify_selected (const drvreg_descriptor_t * p_descriptor)
{
    counter_context_t context   = {40};
    component_t       component = {0};
    assert(
        0 ==
        drvreg_attach(
            p_descriptor, "selected", &context, sizeof(context), &component));
    int value = 0;
    assert((0 == component_read(&component, &value, sizeof(value))) &&
           (41 == value));
    const component_t         original  = component;
    const drvreg_descriptor_t invalid[] = {
        {"", p_descriptor->p_operations, sizeof(context)},
        {"missing_ops", NULL, sizeof(context)},
        {"missing_storage", p_descriptor->p_operations, 0U}};
    for (size_t index = 0U; index < (sizeof(invalid) / sizeof(invalid[0]));
         ++index)
    {
        assert(UTHOME_COMPONENT_ERR_INVALID == drvreg_attach(&invalid[index],
                                                             "selected",
                                                             &context,
                                                             sizeof(context),
                                                             &component));
        assert(0 == memcmp(&original, &component, sizeof(original)));
    }
    assert(UTHOME_COMPONENT_ERR_INVALID ==
           drvreg_attach(p_descriptor, "selected", &context, 0U, &component));
    assert(
        UTHOME_COMPONENT_ERR_INVALID ==
        drvreg_attach(p_descriptor, "", &context, sizeof(context), &component));
    assert(0 == memcmp(&original, &component, sizeof(original)));
}

/** @brief Read p_counter. */
static int read_counter (void * p_context, void * p_data, size_t capacity)
{
    if (capacity < sizeof(int))
    {
        return -1;
    }
    counter_context_t * p_counter = p_context;
    *(int *)p_data                = ++p_counter->value;
    return 0;
}

/* End of file. */

