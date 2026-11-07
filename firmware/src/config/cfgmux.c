/**
 * @file cfgmux.c
 * @brief Lease existing RX storage and preserve controller IDs and retries.
 */
#include "cfgmux.h"
#include <string.h>
static bool token_valid (const char * p_token);
static int outcome (uint32_t status);

/** @brief Bind borrowed owner contexts without restoring or saving anything. */
int cfgmux_init (cfgmux_t * p_mux, cfgctrl_t * p_control, cfgpump_t * p_pump)
{
    if ((NULL == p_mux) || (NULL == p_control) || (NULL == p_pump) ||
        (p_mux->b_initialized) || (!p_control->b_initialized) ||
        (p_pump->p_control != p_control))
    {
        return -1;
    }
    *p_mux = (cfgmux_t){
        .p_control = p_control, .p_pump = p_pump, .b_initialized = true};
    return 0;
}

/** @brief Bind the actual network boot identity exactly once. */
int cfgmux_boot (cfgmux_t * p_mux, uint64_t boot_id)
{
    if ((NULL == p_mux) || (!p_mux->b_initialized) || (p_mux->b_boot))
    {
        return -1;
    }
    p_mux->boot_id = boot_id;
    p_mux->b_boot  = true;
    return 0;
}

/** @brief The application gates USB parsing while HTTP owns its RX storage. */
bool cfgmux_busy (const cfgmux_t * p_mux)
{
    return (NULL != p_mux) && (p_mux->b_lease);
}

/** @brief Reserve one upload only after authentication/framing validation. */
int cfgmux_begin (cfgmux_t *   p_mux,
                  uint64_t     boot_id,
                  uint32_t     request_id,
                  uint32_t     revision,
                  uint32_t     expected,
                  const char * p_token,
                  uint32_t     size)
{
    if ((NULL == p_mux) || (!p_mux->b_initialized) || (!p_mux->b_boot))
    {
        return 503;
    }
    if ((0U == request_id) || (0U == revision) || (0U == size) ||
        (UTHOME_CONFIG_BYTES < size) || (!token_valid(p_token)))
    {
        return 400;
    }
    cfgctrl_t * p_control = p_mux->p_control;
    cfgpump_t * p_pump    = p_mux->p_pump;
    if ((p_mux->b_lease) || (p_control->b_busy) || (p_pump->b_busy) ||
        (p_pump->b_pending) || (0U != p_pump->receiver.header_used) ||
        (p_pump->received_size != p_pump->received_used))
    {
        return 503;
    }
    if (boot_id != p_mux->boot_id)
    {
        return 409;
    }
    const bool b_same =
        (p_mux->b_result) && (0 == strcmp(p_token, p_mux->token));
    bool b_retry = false;
    if (b_same)
    {
        if ((request_id != p_mux->request_id) ||
            (revision != p_mux->revision) || (expected != p_mux->expected) ||
            (size != p_mux->size) || (request_id != p_control->last_id) ||
            (CFGWIRE_APPLY != p_control->last_command) ||
            (size != p_control->cached_size))
        {
            return 409;
        }
        b_retry = true;
    }
    else if ((request_id <= p_control->last_id) ||
             (expected != p_control->active_revision))
    {
        return 409;
    }
    else
    {
        /* A new global ID is still rechecked immediately before dispatch. */
    }
    p_mux->p_upload_token  = p_token;
    p_mux->upload_id       = request_id;
    p_mux->upload_revision = revision;
    p_mux->upload_expected = expected;
    p_mux->upload_size     = size;
    p_mux->used            = 0U;
    p_mux->b_retry         = b_retry;
    p_mux->b_lease         = true;
    return 200;
}

/** @brief Copy bounded body chunks into the leased, existing receive buffer. */
int cfgmux_write (cfgmux_t * p_mux, const uint8_t * p_data, size_t size)
{
    if ((NULL == p_mux) || (!p_mux->b_lease) ||
        ((NULL == p_data) && (0U != size)) ||
        (size > (p_mux->upload_size - p_mux->used)))
    {
        return 400;
    }
    if (0U != size)
    {
        memcpy(&p_mux->p_pump->receiver.data[p_mux->used], p_data, size);
        p_mux->used += (uint32_t)size;
    }
    return 200;
}

/** @brief Only the existing controller validates, activates and persists. */
int cfgmux_finish (cfgmux_t * p_mux, uint32_t * p_reply)
{
    if ((NULL == p_mux) || (NULL == p_reply) || (!p_mux->b_lease) ||
        (p_mux->used != p_mux->upload_size))
    {
        return 400;
    }
    cfgctrl_t *     p_control = p_mux->p_control;
    const uint8_t * p_data    = p_mux->p_pump->receiver.data;
    if (p_control->b_busy)
    {
        cfgmux_abort(p_mux);
        return 503;
    }
    if (p_mux->b_retry)
    {
        const bool b_match =
            (p_control->last_id == p_mux->upload_id) &&
            (p_control->cached_size == p_mux->used) &&
            (0 == memcmp(p_control->cached, p_data, p_mux->used));
        if (b_match)
        {
            memcpy(p_reply, p_mux->reply, sizeof(p_mux->reply));
        }
        cfgmux_abort(p_mux);
        return b_match ? outcome(p_mux->reply[0]) : 409;
    }
    if ((p_mux->upload_id <= p_control->last_id) ||
        (p_mux->upload_expected != p_control->active_revision))
    {
        cfgmux_abort(p_mux);
        return 409;
    }
    const cfgwire_request_t request  = {.request_id = p_mux->upload_id,
                                        .revision   = p_mux->upload_revision,
                                        .size       = p_mux->used,
                                        .p_data     = p_data,
                                        .command    = CFGWIRE_APPLY};
    uint32_t                reply[6] = {0};
    if (0 != cfgctrl_handle(p_control, &request, reply))
    {
        cfgmux_abort(p_mux);
        return 503;
    }
    memcpy(p_mux->reply, reply, sizeof(reply));
    memcpy(p_mux->token,
           p_mux->p_upload_token,
           strlen(p_mux->p_upload_token) + 1U);
    p_mux->request_id = p_mux->upload_id;
    p_mux->revision   = p_mux->upload_revision;
    p_mux->expected   = p_mux->upload_expected;
    p_mux->size       = p_mux->used;
    p_mux->b_result   = true;
    memcpy(p_reply, reply, sizeof(reply));
    cfgmux_abort(p_mux);
    return outcome(reply[0]);
}

/** @brief Release only transport ownership, never cached/durable results. */
void cfgmux_abort (cfgmux_t * p_mux)
{
    if ((NULL != p_mux) && (p_mux->b_lease))
    {
        cfgwire_reset(&p_mux->p_pump->receiver);
        p_mux->p_upload_token = NULL;
        p_mux->used           = 0U;
        p_mux->b_lease        = false;
        p_mux->b_retry        = false;
    }
}

/** @brief Return the latest HTTP result even after a delivery disconnect. */
int cfgmux_result (const cfgmux_t * p_mux,
                   uint32_t         request_id,
                   const char *     p_token,
                   uint32_t *       p_reply)
{
    if ((NULL == p_mux) || (!p_mux->b_initialized) || (NULL == p_reply))
    {
        return 503;
    }
    if ((!p_mux->b_result) || (!token_valid(p_token)) ||
        (request_id != p_mux->request_id) ||
        (0 != strcmp(p_token, p_mux->token)))
    {
        return 404;
    }
    memcpy(p_reply, p_mux->reply, sizeof(p_mux->reply));
    return 200;
}

/** @brief Idempotency tokens are bounded identifiers, not credentials. */
static bool token_valid (const char * p_token)
{
    if (NULL == p_token)
    {
        return false;
    }
    for (size_t index = 0U; index <= 64U; ++index)
    {
        const unsigned char value = (unsigned char)p_token[index];
        if (0U == value)
        {
            return 0U != index;
        }
        if (!((('a' <= value) && ('z' >= value)) ||
              (('A' <= value) && ('Z' >= value)) ||
              (('0' <= value) && ('9' >= value)) || ('-' == value) ||
              ('_' == value) || ('.' == value)))
        {
            return false;
        }
    }
    return false;
}

/** @brief A completed Apply is not necessarily validated, active or saved. */
static int outcome (uint32_t status)
{
    if ((CFGCTRL_CONFLICT == status) || (CFGCTRL_STALE == status))
    {
        return 409;
    }
    if (CFGCTRL_REJECTED == status)
    {
        return 422;
    }
    return (CFGCTRL_SAVED == status) ? 200 : 503;
}

/* End of file. */

