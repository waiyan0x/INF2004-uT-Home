/**
 * @file guard_main.c
 * @brief Runtime target guard regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "guard_main.h"
#include <assert.h>
#include <stdio.h>
#include "uthome/runtime.h"

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_device_t device = {.name             = "sensor",
                               .period_ms        = 100U,
                               .priority         = 10U,
                               .stack_size_bytes = 3072U,
                               .b_enabled        = true};
    assert(UTHOME_RUNTIME_ERR_BACKEND == runtime_init());
    assert(runtime_is_faulted());
    assert(UTHOME_RUNTIME_ERR_BACKEND == runtime_register_device(&device));
    assert(UTHOME_RUNTIME_ERR_BACKEND == runtime_apply_devices(&device, 1));
    assert(UTHOME_RUNTIME_ERR_BACKEND == runtime_tick(100));
    puts("Unimplemented target backend never falls back to host success.");
    return 0;
}

/* End of file. */

