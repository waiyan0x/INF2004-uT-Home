/**
 * @file txwire.h
 * @brief Fixed binary MQTT experiment envelope, independent of node profiles.
 */
#ifndef UTHOME_TXWIRE_H
#define UTHOME_TXWIRE_H
#include "txrule.h"

#define TXWIRE_REQUEST_SIZE 48U
#define TXWIRE_REPLY_SIZE   65U

/* One complete non-retained MQTT payload per call. No pointer is retained.
 * Input/output/state must not overlap. Malformed envelopes return INVALID
 * with a zero request ID when no valid ID was decoded. */
#ifdef __cplusplus
extern "C"
{
#endif

    void txwire_process (txrule_t *      p_state,
                         const uint8_t * p_request,
                         size_t          size,
                         txrule_read_t * p_read,
                         void *          p_context,
                         uint8_t *       p_reply);
#ifdef __cplusplus
}
#endif

#endif

/* End of file. */

