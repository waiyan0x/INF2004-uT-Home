/**
 * @file plan_main.c
 * @brief Runtime plan fixture regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
/* Test-only plan projection. Both registry entries are counter fixtures, not
 * BMP388 or HC-SR04 implementations. No wiring or driver activation is tested.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "plan_main.h"
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uthome/runtime.h"
#include "uthome/drvreg.h"

#define FIXTURE_FIELD_COUNT 6U
#define FIXTURE_TICK_COUNT  100U

typedef struct
{
    uint32_t reads;
} fixture_t;

static int read_sample (void * p_context, void * p_data, size_t capacity);
static int step (component_t * p_component, void * p_unused);

/** @brief Run the regression scenarios and report success. */
int main (int argc, char ** pp_argv)
{
    /* driver, instance, period, priority, stack, enabled per normalized device
     */
    if (0 != (argc - 1) % (int)FIXTURE_FIELD_COUNT)
    {
        return 2;
    }
    size_t count = (size_t)(argc - 1) / FIXTURE_FIELD_COUNT;
    if (count > UTHOME_RUNTIME_MAX_DEVICES)
    {
        return 2;
    }
    const component_operations_t operations = {.p_read = read_sample};
    const drvreg_descriptor_t    drivers[]  = {
        {"bmp388", &operations, sizeof(fixture_t)},
        {"hc_sr04", &operations, sizeof(fixture_t)}};
    drvreg_registry_t registry                             = {0};
    fixture_t         contexts[UTHOME_RUNTIME_MAX_DEVICES] = {0U};
    runtime_device_t  devices[UTHOME_RUNTIME_MAX_DEVICES]  = {0};
    runtime_binding_t bindings[UTHOME_RUNTIME_MAX_DEVICES] = {0};
    assert(0 == drvreg_init(&registry, drivers, 2));
    assert(0 == runtime_init());
    for (size_t index = 0U; index < count; ++index)
    {
        char ** pp_fields = &pp_argv[1U + index * FIXTURE_FIELD_COUNT];
        if (strlen(pp_fields[1]) >= sizeof(devices[index].name))
        {
            return 2;
        }
        strcpy(devices[index].name, pp_fields[1]);
        devices[index].period_ms = (uint32_t)strtoul(pp_fields[2], NULL, 10);
        devices[index].priority  = (uint8_t)strtoul(pp_fields[3], NULL, 10);
        devices[index].stack_size_bytes =
            (size_t)strtoul(pp_fields[4], NULL, 10);
        devices[index].b_enabled = 0U != strtoul(pp_fields[5], NULL, 10);
        if (0 != drvreg_bind(&registry,
                             pp_fields[0],
                             pp_fields[1],
                             &contexts[index],
                             sizeof(contexts[index]),
                             &bindings[index].component))
        {
            return 3;
        }
        bindings[index].p_step = step;
    }
    assert(0 == runtime_apply_bound_devices(devices, bindings, count));
    for (uint32_t index = 0U; index < FIXTURE_TICK_COUNT; ++index)
    {
        assert(0 == runtime_tick(100));
    }
    for (size_t index = 0U; index < count; ++index)
    {
        printf("%s %" PRIu32 "\n", devices[index].name, contexts[index].reads);
    }
    assert(0 == runtime_apply_bound_devices(NULL, NULL, 0));
    return 0;
}

/** @brief Read sample. */
static int read_sample (void * p_context, void * p_data, size_t capacity)
{
    if (capacity < sizeof(uint32_t))
    {
        return -1;
    }
    fixture_t * p_instance = p_context;
    *(uint32_t *)p_data    = ++p_instance->reads;
    return 0;
}

/** @brief Step. */
static int step (component_t * p_component, void * p_unused)
{
    (void)p_unused;
    uint32_t sample = {0U};
    return component_read(p_component, &sample, sizeof(sample));
}

/* End of file. */

