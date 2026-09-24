#include <assert.h>
#include <stdio.h>

#include "../../firmware/src/runtime/runtime_port.h"


int main(void)
{
    uthome_runtime_port_task_id_t task_id;

    uthome_runtime_port_task_config_t config = {
        .priority = 3,
        .stack_size_bytes = 2048,
        .period_ms = 2000
    };

    bool prepared = false;
    bool running = false;


    assert(
        uthome_runtime_port_init() == 0
    );


    /*
     * PREPARE
     */
    assert(
        uthome_runtime_port_task_prepare(
            &config,
            &task_id
        ) == 0
    );


    assert(
        uthome_runtime_port_task_is_prepared(
            task_id,
            &prepared
        ) == 0
    );

    assert(prepared == true);


    /*
     * Prepared does not mean running.
     */
    assert(
        uthome_runtime_port_task_is_running(
            task_id,
            &running
        ) == 0
    );

    assert(running == false);


    /*
     * START
     */
    assert(
        uthome_runtime_port_task_start(
            task_id
        ) == 0
    );


    assert(
        uthome_runtime_port_task_is_running(
            task_id,
            &running
        ) == 0
    );

    assert(running == true);


    /*
     * RECONFIGURE
     */
    config.period_ms = 5000;
    config.stack_size_bytes = 4096;

    assert(
        uthome_runtime_port_task_reconfigure(
            task_id,
            &config
        ) == 0
    );


    /*
     * STOP / RELEASE
     */
    assert(
        uthome_runtime_port_task_stop(
            task_id
        ) == 0
    );


    assert(
        uthome_runtime_port_task_is_prepared(
            task_id,
            &prepared
        ) == 0
    );

    assert(prepared == false);


    printf(
        "All runtime port tests passed!\n"
    );

    return 0;
}