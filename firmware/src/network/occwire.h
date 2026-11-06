/** @file occwire.h
 * @brief Single-client read-only ESPHome native API 1.9 lab codec. */
#ifndef UTHOME_OCCWIRE_H
#define UTHOME_OCCWIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t receive[128];
    uint8_t transmit[128];
    uint8_t used;
    uint8_t queued;
    uint8_t sent;
    uint8_t last;
    bool    b_hello;
    bool    b_subscribed;
    bool    b_closing;
} occwire_t;

/* Zero initialize on each new client. Call only with no pending output.
 * State: 0 missing, 1 clear, 2 occupied. Input may contain partial frames.
 * Negative return means drop the session, never retry a malformed frame. */
int occwire_step (occwire_t * p_wire, const char * p_node, uint8_t state);

#endif

/* End of file. */

