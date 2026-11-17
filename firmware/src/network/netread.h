/**
 * @file netread.h
 * @brief Shared MQTT/BLE metadata and caller-provided sensor snapshots.
 */
#ifndef UTHOME_NETREAD_H
#define UTHOME_NETREAD_H
#include "netble.h"

typedef struct
{
    netble_meta_t meta;
    uint32_t      saved_revision;
    uint32_t      runtime_state;
    bool          b_durable_known;
    bool          b_wifi_online;
    char          node_id[32];
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
#endif

/* End of file. */

