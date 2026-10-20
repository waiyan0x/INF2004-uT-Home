/** @file relayctl.h
 * @brief Maintain a steady relay command with polarity and switching holdoff.
 */
#ifndef UTHOME_RELAYCTL_H
#define UTHOME_RELAYCTL_H
#include "digiout.h"
#include <stdint.h>

typedef struct
{
    digiout_t output;
    uint32_t  minimum_ms;
    bool      b_active_high;
} relayctl_config_t;

typedef struct
{
    relayctl_config_t config;
    uint64_t          last_ms;
    uint64_t          changed_ms;
    bool              b_open;
    bool              b_on;
    bool              b_fault;
} relayctl_t;

/* Zero-initialize; one task owns all calls. Borrow an exclusively claimed,
 * configured digital line initially at the inactive level. No pin allocation
 * occurs here. Context/line must remain alive until close succeeds.
 * Return 0 success, -1 invalid state/config, -2 write failure, -3 clock
 * rollback. b_on is the last successful command, NOT physical fan feedback.
 * Failed writes latch a fault; step then retries OFF and never energizes until
 * close succeeds and the lifecycle owner reopens the module. */
int relayctl_open (relayctl_t *              p_relay,
                   const relayctl_config_t * p_config,
                   uint64_t                  now_ms);
/* Ordinary changes obey minimum_ms. Invalid/stale input (b_permitted=false)
 * bypasses holdoff to request OFF and restarts the reactivation holdoff.
 * A suppressed normal transition returns 0; inspect b_on for applied command.
 */
int relayctl_step (relayctl_t * p_relay,
                   bool         b_requested,
                   bool         b_permitted,
                   uint64_t     now_ms);
/* Unconditionally write inactive; retain ownership if it fails. */
int relayctl_close (relayctl_t * p_relay);
#endif
/* End of file. */

