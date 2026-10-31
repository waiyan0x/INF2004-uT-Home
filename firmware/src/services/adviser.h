/**
 * @file adviser.h
 * @brief One bounded read-only model request in an existing controller task.
 */
#ifndef UTHOME_ADVISER_H
#define UTHOME_ADVISER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADVISER_CANDIDATES  4U
#define ADVISER_FEATURES    18U
#define ADVISER_HASH_BYTES  32U
#define ADVISER_CONTEXTS    8U
#define ADVISER_TIMEOUT_US  UINT64_C(100000)
#define ADVISER_LIFETIME_US UINT64_C(30000000)

typedef enum
{
    ADVISER_IDLE = 0,
    ADVISER_BUSY,
    ADVISER_READY,
    ADVISER_NO_ADVICE,
    ADVISER_UNSUPPORTED,
    ADVISER_STALE,
    ADVISER_TIMEOUT,
    ADVISER_ERROR,
    ADVISER_INVALID
} adviser_status_t;

typedef struct
{
    uint8_t  build[ADVISER_HASH_BYTES];
    uint8_t  context[ADVISER_HASH_BYTES];
    uint32_t revision;
    uint32_t profile_crc;
    uint32_t session;
} adviser_identity_t;

typedef int adviser_predict_t (uint32_t const * p_features, size_t count);

typedef struct
{
    uint8_t             hash[ADVISER_HASH_BYTES];
    uint8_t             build[ADVISER_HASH_BYTES];
    uint8_t const *     p_contexts;
    size_t              context_count;
    adviser_predict_t * p_predict;
    bool                b_qualified;
} adviser_model_t;

typedef struct
{
    adviser_identity_t identity;
    uint32_t           request_id;
    uint32_t           intervals[ADVISER_CANDIDATES];
    uint32_t           features[ADVISER_CANDIDATES][ADVISER_FEATURES];
    uint32_t           count;
    bool               b_admitted;
} adviser_request_t;

typedef struct
{
    adviser_identity_t identity;
    uint8_t            model_hash[ADVISER_HASH_BYTES];
    uint32_t           request_id;
    uint32_t           interval_ms;
    adviser_status_t   status;
} adviser_reply_t;

typedef struct
{
    adviser_request_t       request;
    adviser_model_t const * p_model;
    uint64_t                started_us;
    uint64_t                last_us;
    uint32_t                cursor;
    uint32_t                supported;
    uint32_t                best_ms;
    adviser_status_t        status;
} adviser_t;

/**
 * @brief Start in zero-initialized caller storage; BUSY preserves the old
 * request.
 *
 * One serialized controller owns every call. The request is copied. Its
 * features must be reconstructed from admitted typed settings, never accepted
 * from a transport client. Identity context binds the catalogue, fixed workload
 * and resource/radio configuration. Every candidate needs full normal
 * admission. A model descriptor and its context array are immutable for the
 * boot lifetime; b_qualified is a compiled release decision, not a request
 * field. Existing experimental exports must keep it false. No task, lock, heap
 * or Apply occurs.
 */
adviser_status_t adviser_start (adviser_t *               p_state,
                                adviser_model_t const *   p_model,
                                adviser_request_t const * p_request,
                                uint64_t                  now_us);

/** @brief Evaluate at most one candidate; recheck identity and clock each step.
 */
adviser_status_t adviser_step (adviser_t *                p_state,
                               adviser_identity_t const * p_current,
                               uint64_t                   now_us);

/** @brief Return no interval while busy, invalid, expired or stale. */
int adviser_reply (adviser_t *                p_state,
                   adviser_identity_t const * p_current,
                   uint64_t                   now_us,
                   adviser_reply_t *          p_reply);

/** @brief Invalidate a request on disconnect or controller lifecycle changes.
 */
void adviser_cancel (adviser_t * p_state);

#endif

/* End of file. */

