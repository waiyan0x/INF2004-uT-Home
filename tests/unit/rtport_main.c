/**
 * @file rtport_main.c
 * @brief Runtime port regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtport_main.h"
#include <assert.h>
#include <stdio.h>

#include "../../firmware/src/runtime/rt_port.h"

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    rt_port_task_id_t h_task = {0};

    rt_port_task_config_t config = {
        .priority = 3U, .stack_size_bytes = 2048U, .period_ms = 2000U};

    bool b_prepared = false;
    bool b_running  = false;

    assert(0 == rt_port_init());

    /*
     * PREPARE
     */
    assert(0 == rt_port_task_prepare(&config, &h_task));

    assert(0 == rt_port_task_is_prepared(h_task, &b_prepared));

    assert(true == b_prepared);

    /*
     * Prepared does not mean b_running.
     */
    assert(0 == rt_port_task_is_running(h_task, &b_running));

    assert(false == b_running);

    /*
     * START
     */
    assert(0 == rt_port_task_start(h_task));

    assert(0 == rt_port_task_is_running(h_task, &b_running));

    assert(true == b_running);

    /*
     * RECONFIGURE
     */
    config.period_ms        = 5000U;
    config.stack_size_bytes = 4096U;

    assert(0 == rt_port_task_reconfigure(h_task, &config));

    /*
     * STOP / RELEASE
     */
    assert(0 == rt_port_task_stop(h_task));

    assert(0 == rt_port_task_is_prepared(h_task, &b_prepared));

    assert(false == b_prepared);

    printf("All runtime port tests passed!\n");

    return 0;
}

/* End of file. */

