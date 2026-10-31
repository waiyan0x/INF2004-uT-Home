/**
 * @file adviser.c
 * @brief Bound inference work and reject advice from stale or unknown contexts.
 */
#include "adviser.h"

static bool equal_hash (uint8_t const * p_left, uint8_t const * p_right);
static bool same_identity (adviser_identity_t const * p_left,
                           adviser_identity_t const * p_right);
static bool valid_request (adviser_request_t const * p_request);
static bool supported_model (adviser_model_t const *    p_model,
                             adviser_identity_t const * p_identity);
static void recheck (adviser_t *                p_state,
                     adviser_identity_t const * p_current,
                     uint64_t                   now_us);

/** @brief Copy one admitted request; unqualified models never execute. */
adviser_status_t adviser_start (adviser_t *               p_state,
                                adviser_model_t const *   p_model,
                                adviser_request_t const * p_request,
                                uint64_t                  now_us)
{
    if (NULL == p_state)
    {
        return ADVISER_INVALID;
    }
    if (ADVISER_BUSY == p_state->status)
    {
        return ADVISER_BUSY;
    }
    if (!valid_request(p_request))
    {
        return ADVISER_INVALID;
    }
    p_state->request    = *p_request;
    p_state->p_model    = p_model;
    p_state->started_us = now_us;
    p_state->last_us    = now_us;
    p_state->cursor     = 0U;
    p_state->supported  = 0U;
    p_state->best_ms    = 0U;
    p_state->status     = supported_model(p_model, &p_request->identity)
                              ? ADVISER_BUSY
                              : ADVISER_UNSUPPORTED;
    return p_state->status;
}

/** @brief A candidate cannot apply itself or bypass ordinary configuration. */
adviser_status_t adviser_step (adviser_t *                p_state,
                               adviser_identity_t const * p_current,
                               uint64_t                   now_us)
{
    if ((NULL == p_state) || (NULL == p_current))
    {
        return ADVISER_INVALID;
    }
    recheck(p_state, p_current, now_us);
    if (ADVISER_BUSY == p_state->status)
    {
        const uint32_t index  = p_state->cursor;
        const int      result = p_state->p_model->p_predict(
            p_state->request.features[index], ADVISER_FEATURES);
        if ((-1 > result) || (1 < result))
        {
            p_state->best_ms = 0U;
            p_state->status  = ADVISER_ERROR;
        }
        else
        {
            if (0 <= result)
            {
                ++p_state->supported;
            }
            if ((1 == result) &&
                (p_state->best_ms < p_state->request.intervals[index]))
            {
                p_state->best_ms = p_state->request.intervals[index];
            }
            ++p_state->cursor;
            if (p_state->cursor == p_state->request.count)
            {
                p_state->status =
                    (0U == p_state->supported)
                        ? ADVISER_UNSUPPORTED
                        : ((0U == p_state->best_ms) ? ADVISER_NO_ADVICE
                                                    : ADVISER_READY);
            }
        }
    }
    return p_state->status;
}

/** @brief Bind the result to the originating profile, session, build and model.
 */
int adviser_reply (adviser_t *                p_state,
                   adviser_identity_t const * p_current,
                   uint64_t                   now_us,
                   adviser_reply_t *          p_reply)
{
    if ((NULL == p_state) || (NULL == p_current) || (NULL == p_reply))
    {
        return -1;
    }
    recheck(p_state, p_current, now_us);
    *p_reply = (adviser_reply_t){
        .identity   = p_state->request.identity,
        .request_id = p_state->request.request_id,
        .status     = p_state->status,
        .interval_ms =
            (ADVISER_READY == p_state->status) ? p_state->best_ms : 0U};
    if (NULL != p_state->p_model)
    {
        for (size_t index = 0U; index < ADVISER_HASH_BYTES; ++index)
        {
            p_reply->model_hash[index] = p_state->p_model->hash[index];
        }
    }
    return 0;
}

/** @brief Keep provenance for a stale reply, but discard any recommendation. */
void adviser_cancel (adviser_t * p_state)
{
    if (NULL != p_state)
    {
        p_state->status  = ADVISER_STALE;
        p_state->best_ms = 0U;
    }
}

/** @brief These public hashes are provenance, not secret authenticators. */
static bool equal_hash (uint8_t const * p_left, uint8_t const * p_right)
{
    for (size_t index = 0U; index < ADVISER_HASH_BYTES; ++index)
    {
        if (p_left[index] != p_right[index])
        {
            return false;
        }
    }
    return true;
}

/** @brief Revision alone cannot bind drafts, firmware, reconnects or contexts.
 */
static bool same_identity (adviser_identity_t const * p_left,
                           adviser_identity_t const * p_right)
{
    return ((p_left->revision == p_right->revision) &&
            (p_left->profile_crc == p_right->profile_crc) &&
            (p_left->session == p_right->session) &&
            (equal_hash(p_left->build, p_right->build)) &&
            (equal_hash(p_left->context, p_right->context)));
}

/** @brief Reject incomplete, oversized, duplicate or inconsistent candidates.
 */
static bool valid_request (adviser_request_t const * p_request)
{
    if ((NULL == p_request) || (!p_request->b_admitted) ||
        (0U == p_request->request_id) || (0U == p_request->identity.session) ||
        (0U == p_request->count) || (ADVISER_CANDIDATES < p_request->count))
    {
        return false;
    }
    for (uint32_t index = 0U; index < p_request->count; ++index)
    {
        if ((0U == p_request->intervals[index]) ||
            (p_request->intervals[index] != p_request->features[index][1]))
        {
            return false;
        }
        for (uint32_t previous = 0U; previous < index; ++previous)
        {
            if (p_request->intervals[index] == p_request->intervals[previous])
            {
                return false;
            }
        }
    }
    return true;
}

/** @brief No unknown build/context or experimental export can grant advice. */
static bool supported_model (adviser_model_t const *    p_model,
                             adviser_identity_t const * p_identity)
{
    if ((NULL == p_model) || (!p_model->b_qualified) ||
        (NULL == p_model->p_predict) || (NULL == p_model->p_contexts) ||
        (0U == p_model->context_count) ||
        (ADVISER_CONTEXTS < p_model->context_count) ||
        (!equal_hash(p_model->build, p_identity->build)))
    {
        return false;
    }
    for (size_t index = 0U; index < p_model->context_count; ++index)
    {
        if (equal_hash(&p_model->p_contexts[index * ADVISER_HASH_BYTES],
                       p_identity->context))
        {
            return true;
        }
    }
    return false;
}

/** @brief Fail closed on time reversal, changed context or bounded lifetime. */
static void recheck (adviser_t *                p_state,
                     adviser_identity_t const * p_current,
                     uint64_t                   now_us)
{
    if (ADVISER_IDLE == p_state->status)
    {
        return;
    }
    if (!same_identity(&p_state->request.identity, p_current))
    {
        adviser_cancel(p_state);
    }
    else if ((now_us < p_state->last_us) || (now_us < p_state->started_us))
    {
        p_state->status  = ADVISER_ERROR;
        p_state->best_ms = 0U;
    }
    else if (((ADVISER_BUSY == p_state->status) &&
              ((now_us - p_state->started_us) >= ADVISER_TIMEOUT_US)) ||
             ((now_us - p_state->started_us) >= ADVISER_LIFETIME_US))
    {
        p_state->status  = ADVISER_TIMEOUT;
        p_state->best_ms = 0U;
    }
    else
    {
        /* The original context and bounded lifetime are still valid. */
    }
    p_state->last_us = now_us;
}

/* End of file. */

