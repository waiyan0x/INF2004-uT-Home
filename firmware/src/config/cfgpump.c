/**
 * @file cfgpump.c
 * @brief Preserve stream fragments and backpressure around one request.
 */
#include "cfgpump.h"
static int make_reply (cfgpump_t * p_pump, const cfgwire_request_t * p_request);
static int receive_request (cfgpump_t * p_pump, uint64_t now_ms);
static int transmit_reply (cfgpump_t * p_pump, uint64_t now_ms);
static void discard_transport (cfgpump_t * p_pump);

/** @brief Bind complete, explicitly supplied dependencies without I/O. */
int cfgpump_init (cfgpump_t *           p_pump,
                  cfgctrl_t *           p_control,
                  const cfgpump_ops_t * p_operations,
                  void *                p_context)
{
    if ((NULL == p_pump) || (NULL == p_control) ||
        (!p_control->b_initialized) || (NULL == p_operations) ||
        (NULL == p_operations->p_read) || (NULL == p_operations->p_write) ||
        (NULL != p_pump->p_control))
    {
        return -2;
    }
    p_pump->p_control    = p_control;
    p_pump->p_operations = p_operations;
    p_pump->p_context    = p_context;
    return 0;
}

/** @brief Bind a read-only observer without changing the controller contract.
 */
int cfgpump_observe (cfgpump_t *         p_pump,
                     cfgpump_observe_t * p_observe,
                     void *              p_context)
{
    if ((NULL == p_pump) || (NULL == p_pump->p_control) ||
        (NULL == p_observe) || (p_pump->b_busy) || (p_pump->b_pending))
    {
        return -2;
    }
    p_pump->p_observe  = p_observe;
    p_pump->p_observer = p_context;
    return 0;
}

/** @brief Keep capability discovery read-only and separate from telemetry. */
int cfgpump_capabilities (cfgpump_t *         p_pump,
                          cfgpump_observe_t * p_observe,
                          void *              p_context)
{
    if ((NULL == p_pump) || (NULL == p_pump->p_control) ||
        (NULL == p_observe) || (p_pump->b_busy) || (p_pump->b_pending))
    {
        return -2;
    }
    p_pump->p_capabilities       = p_observe;
    p_pump->p_capability_context = p_context;
    return 0;
}

/** @brief Bind an explicitly requested hardware observer while idle. */
int cfgpump_discovery (cfgpump_t *          p_pump,
                       cfgpump_discover_t * p_discover,
                       void *               p_context)
{
    if ((NULL == p_pump) || (NULL == p_pump->p_control) ||
        (NULL == p_discover) || (p_pump->b_busy) || (p_pump->b_pending))
    {
        return -2;
    }
    p_pump->p_discover          = p_discover;
    p_pump->p_discovery_context = p_context;
    return 0;
}

/** @brief Advance one bounded transport step from the controller's task. */
int cfgpump_step (cfgpump_t * p_pump, uint64_t now_ms)
{
    if ((NULL == p_pump) || (NULL == p_pump->p_control) || (p_pump->b_busy))
    {
        return -2;
    }
    p_pump->b_busy = true;
    int result     = 0;
    if (p_pump->b_pending)
    {
        result = transmit_reply(p_pump, now_ms);
    }
    else
    {
        result = receive_request(p_pump, now_ms);
    }
    p_pump->b_busy = false;
    return result;
}

/** @brief Consume retained input before reading another chunk. */
static int receive_request (cfgpump_t * p_pump, uint64_t now_ms)
{
    /* Timeout is a framing rejection, not a reason to stop listening. */
    (void)cfgwire_expire(&p_pump->receiver, now_ms);
    if (p_pump->received_used == p_pump->received_size)
    {
        p_pump->received_used = 0U;
        p_pump->received_size = 0U;
        if ((0 != p_pump->p_operations->p_read(p_pump->p_context,
                                               p_pump->received,
                                               sizeof(p_pump->received),
                                               &p_pump->received_size)) ||
            (p_pump->received_size > sizeof(p_pump->received)))
        {
            discard_transport(p_pump);
            return -1;
        }
    }
    while (p_pump->received_used < p_pump->received_size)
    {
        const uint8_t byte = p_pump->received[p_pump->received_used];
        p_pump->received_used++;
        if (1 == cfgwire_feed(&p_pump->receiver, byte, now_ms))
        {
            cfgwire_request_t request = {0};
            if ((0 != cfgwire_request(&p_pump->receiver, &request)) ||
                (0 != make_reply(p_pump, &request)))
            {
                discard_transport(p_pump);
                return -1;
            }
            cfgwire_reset(&p_pump->receiver);
            p_pump->sent           = 0U;
            p_pump->reply_start_ms = UINT64_MAX;
            p_pump->b_pending      = true;
            break;
        }
    }
    return 0;
}

/** @brief Never busy-wait on a full or disconnected transport. */
static int transmit_reply (cfgpump_t * p_pump, uint64_t now_ms)
{
    if (UINT64_MAX == p_pump->reply_start_ms)
    {
        p_pump->reply_start_ms = now_ms;
    }
    if ((now_ms < p_pump->reply_start_ms) ||
        ((now_ms - p_pump->reply_start_ms) >= UTHOME_CFGWIRE_TIMEOUT))
    {
        discard_transport(p_pump);
        return -1;
    }
    size_t       count     = 0U;
    const size_t unsent    = p_pump->reply_size - p_pump->sent;
    const size_t remaining = (unsent < 48U) ? unsent : 48U;
    if ((0 != p_pump->p_operations->p_write(p_pump->p_context,
                                            &p_pump->reply[p_pump->sent],
                                            remaining,
                                            &count)) ||
        (count > remaining))
    {
        discard_transport(p_pump);
        return -1;
    }
    p_pump->sent += count;
    if (p_pump->reply_size == p_pump->sent)
    {
        p_pump->b_pending = false;
    }
    return 0;
}

/** @brief Leave all live and durable configuration metadata intact. */
static void discard_transport (cfgpump_t * p_pump)
{
    cfgwire_reset(&p_pump->receiver);
    p_pump->received_used = 0U;
    p_pump->received_size = 0U;
    p_pump->sent          = 0U;
    p_pump->b_pending     = false;
}

/** @brief Observations never consume IDs, call storage or evict retry state. */
static int make_reply (cfgpump_t * p_pump, const cfgwire_request_t * p_request)
{
    if (CFGWIRE_DISCOVER == p_request->command)
    {
        size_t size = 0U;
        if ((NULL == p_pump->p_discover) ||
            (0 != p_pump->p_discover(p_pump->p_discovery_context,
                                     p_request,
                                     &p_pump->reply[UTHOME_CFGWIRE_HEADER],
                                     UTHOME_CFGWIRE_PAYLOAD,
                                     &size)) ||
            (0 != cfgwire_observation(
                      p_request, p_pump->reply, sizeof(p_pump->reply), size)))
        {
            return -1;
        }
        p_pump->reply_size = UTHOME_CFGWIRE_HEADER + size;
        return 0;
    }
    if ((CFGWIRE_TELEMETRY == p_request->command) ||
        (CFGWIRE_CAPABILITIES == p_request->command))
    {
        const bool          b_caps = CFGWIRE_CAPABILITIES == p_request->command;
        cfgpump_observe_t * p_observe =
            b_caps ? p_pump->p_capabilities : p_pump->p_observe;
        void * p_context =
            b_caps ? p_pump->p_capability_context : p_pump->p_observer;
        size_t size = 0U;
        if ((NULL == p_observe) ||
            (0 != p_observe(p_context,
                            &p_pump->reply[UTHOME_CFGWIRE_HEADER],
                            UTHOME_CFGWIRE_PAYLOAD,
                            &size)) ||
            (0 != cfgwire_observation(
                      p_request, p_pump->reply, sizeof(p_pump->reply), size)))
        {
            return -1;
        }
        p_pump->reply_size = UTHOME_CFGWIRE_HEADER + size;
        return 0;
    }
    uint32_t fields[6] = {0U};
    p_pump->reply_size = UTHOME_CFGWIRE_REPLY;
    if (0 != cfgctrl_handle(p_pump->p_control, p_request, fields))
    {
        return -1;
    }
    return cfgwire_reply(p_request, fields, p_pump->reply);
}

/* End of file. */

