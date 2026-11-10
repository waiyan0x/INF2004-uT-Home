/** @file occwire.h
 * @brief Single-client read-only ESPHome native API 1.9 lab codec. */
#ifndef UTHOME_OCCWIRE_H
#define UTHOME_OCCWIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "uthome/txjob.h"

typedef struct
{
    uint8_t  receive[128];
    uint8_t  transmit[128];
    uint8_t  used;
    uint8_t  queued;
    uint8_t  sent;
    uint8_t  last;
    bool     b_hello;
    bool     b_subscribed;
    bool     b_closing;
    uint32_t last_job;
    uint8_t  listing;
    bool     b_job_sent;
    bool     b_job_available;
} occwire_t;

/* Zero initialize on each new client. Call only with no pending output.
 * State: 0 missing, 1 clear, 2 occupied. Input may contain partial frames.
 * Negative return means drop the session, never retry a malformed frame. */
int occwire_step (occwire_t * p_wire, const char * p_node, uint8_t state);
/* Same framing, with an optional copied transaction result. Missing/stale
 * results send missing_state, independently of radar freshness. */
int occwire_transaction (occwire_t *          p_wire,
                         const char *         p_node,
                         uint8_t              state,
                         const txjob_view_t * p_view,
                         uint64_t             now_ms);

#endif

/* End of file. */

