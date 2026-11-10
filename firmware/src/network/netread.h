/**
 * @file netread.h
 * @brief Authenticated read dispatch over a caller-provided sensor snapshot.
 */
#ifndef UTHOME_NETREAD_H
#define UTHOME_NETREAD_H
#include "nethttp.h"
#include "netble.h"
#include "netbroker.h"
struct txjob_view;

typedef struct
{
    netble_meta_t             meta;
    uint32_t                  saved_revision;
    uint32_t                  runtime_state;
    bool                      b_durable_known;
    bool                      b_wifi_online;
    char                      node_id[32];
    netbroker_view_t          mqtt;
    bool                      b_mqtt_enabled;
    bool                      b_configuration_supported;
    const struct txjob_view * p_transaction;
} netread_info_t;

/* Callback copies one registered source and capture time under the existing
 * shared guard. 0 copied, 1 absent, negative busy/failure. Never dequeue.
 * Production adapter may delegate to shared_source. Metadata and callback must
 * share a serialized configuration owner: no plan change during dispatch.
 */
typedef int netread_capture_t (void *              p_context,
                               uint16_t            source,
                               sampcache_entry_t * p_entry,
                               uint64_t *          p_now);
/* Complete response in caller-owned storage; no socket/SDK/controller work.
 * Identity/status JSON, telemetry canonical 112-byte binary sensor frame.
 * Capture failures return HTTP 503, absent sources 404. Status ==200 parser
 * admission and valid metadata required; all other terminal parser errors
 * yield fixed error JSON. No supported mutation route exists.
 */
int netread_reply (nethttp_t const *      p_http,
                   netread_info_t const * p_info,
                   netread_capture_t *    p_capture,
                   void *                 p_context,
                   uint8_t *              p_output,
                   size_t                 capacity,
                   size_t *               p_size);
#endif

/* End of file. */

