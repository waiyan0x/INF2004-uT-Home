/**
 * @file hostport.h
 * @brief Expose deterministic host-port failure injection controls.
 */
#ifndef UTHOME_HOSTPORT_H
#define UTHOME_HOSTPORT_H
#include "rt_port.h"

typedef enum
{
    UTHOME_HOST_PREPARE,
    UTHOME_HOST_START,
    UTHOME_HOST_STOP,
    UTHOME_HOST_RECONFIGURE,
    UTHOME_HOST_ROLLBACK,
    UTHOME_HOST_OPERATION_COUNT
} hostport_operation_t;

/* Test controls exist only in the host backend. Fail once after N successful
 * calls to the selected operation; resource state is untouched by that failure.
 */
void hostport_fail_after (hostport_operation_t operation, size_t calls);
void hostport_clear_faults (void);
size_t hostport_task_count (void);
#endif

/* End of file. */

