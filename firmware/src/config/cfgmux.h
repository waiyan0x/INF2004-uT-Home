/**
 * @file cfgmux.h
 * @brief Serialize HTTP uploads with the existing USB/controller owner.
 */
#ifndef UTHOME_CFGMUX_H
#define UTHOME_CFGMUX_H
#include "cfgpump.h"

typedef struct
{
    cfgctrl_t *  p_control;
    cfgpump_t *  p_pump;
    const char * p_upload_token;
    uint64_t     boot_id;
    uint32_t     reply[6];
    uint32_t     request_id;
    uint32_t     revision;
    uint32_t     expected;
    uint32_t     size;
    uint32_t     upload_id;
    uint32_t     upload_revision;
    uint32_t     upload_expected;
    uint32_t     upload_size;
    uint32_t     used;
    char         token[65];
    bool         b_initialized;
    bool         b_boot;
    bool         b_lease;
    bool         b_retry;
    bool         b_result;
} cfgmux_t;

/* One controller-task owner; no task/lock/heap or second Apply/save. USB IDs
 * remain unchanged. HTTP uses the same per-boot strictly increasing sequence.
 * The app must skip cfgpump_step while leased. A lease borrows only an idle
 * pump's existing RX data; the HTTP transport enforces its absolute deadline
 * and aborts on disconnect. Never call cfgwire/pump reset while leased.
 */
int cfgmux_init (cfgmux_t * p_mux, cfgctrl_t * p_control, cfgpump_t * p_pump);
int cfgmux_boot (cfgmux_t * p_mux, uint64_t boot_id);
bool cfgmux_busy (const cfgmux_t * p_mux);
int cfgmux_begin (cfgmux_t *   p_mux,
                  uint64_t     boot_id,
                  uint32_t     request_id,
                  uint32_t     revision,
                  uint32_t     expected,
                  const char * p_token,
                  uint32_t     size);
int cfgmux_write (cfgmux_t * p_mux, const uint8_t * p_data, size_t size);
/* HTTP status: 200 saved, 422 rejected, 409 conflict/stale, 503 recovery/
 * storage/unsaved. Result is retained even when delivery fails. Exact retries
 * compare all body bytes, and never call the controller twice. After another
 * request evicts controller bytes, POST retry is 409; GET result may remain.
 */
int cfgmux_finish (cfgmux_t * p_mux, uint32_t * p_reply);
void cfgmux_abort (cfgmux_t * p_mux);
/* 200 means a retained result was found, not that it is saved/current. */
int cfgmux_result (const cfgmux_t * p_mux,
                   uint32_t         request_id,
                   const char *     p_token,
                   uint32_t *       p_reply);
#endif

/* End of file. */

