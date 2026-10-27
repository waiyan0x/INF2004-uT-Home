/**
 * @file c3cost.h
 * @brief Observe one synchronous transfer without changing its result.
 */
#ifndef UTHOME_C3COST_H
#define UTHOME_C3COST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint64_t started_us;
    uint64_t acquired_us;
    bool     b_acquired;
    bool     b_valid;
} c3cost_t;

/* Automatic storage belongs to this call. Record lock acquisition separately;
 * a failed lock has only wait time. Payload bytes count successful transfers,
 * excluding framing/address bits. Active time includes the release call.
 * Setup/discovery calls outside a worker do not enter worker totals. */
void c3cost_begin (c3cost_t * p_state);
void c3cost_acquired (c3cost_t * p_state);
void c3cost_end (const c3cost_t * p_state,
                 size_t           bytes,
                 bool             b_timeout,
                 bool             b_error);

#endif

/* End of file. */

