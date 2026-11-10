/**
 * @file netbroker.c
 * @brief Bound broker handshake, publication, keepalive and outage recovery.
 */
#include "netbroker.h"
#include <string.h>

static size_t text_size (const char * p_text, size_t maximum, bool b_topic);
static void field (uint8_t * p_data, size_t * p_offset, const char * p_text);
static void connect_packet (netbroker_t * p_state);
static int fail (netbroker_t * p_state,
                 uint64_t      now_ms,
                 uint32_t      jitter_ms,
                 bool          b_failure);
static void discard (netbroker_t * p_state, uint32_t count);
static int write_packet (netbroker_t * p_state, uint64_t now_ms);
static int read_packet (netbroker_t * p_state, uint64_t now_ms);

/** @brief Reject incomplete transport or immutable identity before I/O. */
int netbroker_init (netbroker_t * p_state, const netbroker_config_t * p_config)
{
    if ((NULL == p_state) || (NULL == p_config) || (p_state->b_initialized) ||
        (NULL == p_config->p_connect) || (NULL == p_config->p_read) ||
        (NULL == p_config->p_write) || (NULL == p_config->p_close) ||
        (0U == text_size(p_config->p_client, 32U, false)) ||
        (0U == text_size(p_config->p_user, 64U, false)) ||
        (0U == text_size(p_config->p_password, 64U, false)) ||
        (0U == text_size(p_config->p_availability, 128U, true)))
    {
        return -1;
    }
    *p_state = (netbroker_t){.config = *p_config, .b_initialized = true};
    return netretry_init(&p_state->retry, 0U);
}

/** @brief At most one connect/read/write operation per owner iteration. */
int netbroker_step (netbroker_t * p_state,
                    uint64_t      now_ms,
                    bool          b_upstream,
                    uint32_t      generation,
                    uint32_t      jitter_ms)
{
    int admitted = 0;
    int result   = 0;
    if ((NULL == p_state) || (!p_state->b_initialized) ||
        (now_ms < p_state->last_ms) || (now_ms > (UINT64_MAX - 60000U)) ||
        (250U < jitter_ms))
    {
        return -1;
    }
    p_state->last_ms = now_ms;
    if ((!b_upstream) || (generation != p_state->generation))
    {
        if (NETBROKER_OFF != p_state->state)
        {
            (void)fail(p_state, now_ms, jitter_ms, !b_upstream);
        }
        p_state->generation = generation;
    }
    admitted = netretry_poll(&p_state->retry, now_ms, b_upstream, jitter_ms);
    if (0 > admitted)
    {
        return -1;
    }
    if ((!b_upstream) || ((NETBROKER_OFF != p_state->state) &&
                          (NETRETRY_WAITING == p_state->retry.state)))
    {
        if (NETBROKER_OFF != p_state->state)
        {
            return fail(p_state, now_ms, jitter_ms, true);
        }
        return 0;
    }
    if (1 == admitted)
    {
        p_state->state       = NETBROKER_TLS;
        p_state->deadline_ms = now_ms + 30000U;
    }
    if (NETBROKER_OFF == p_state->state)
    {
        return 0;
    }
    if (((NETBROKER_READY != p_state->state) || (0U != p_state->size)) &&
        (now_ms >= p_state->deadline_ms))
    {
        return fail(p_state, now_ms, jitter_ms, true);
    }
    if ((p_state->b_ping) && ((now_ms - p_state->ping_ms) >= 10000U))
    {
        return fail(p_state, now_ms, jitter_ms, true);
    }
    if (NETBROKER_TLS == p_state->state)
    {
        result = p_state->config.p_connect(p_state->config.p_context);
        if (1 == result)
        {
            connect_packet(p_state);
            p_state->state = NETBROKER_CONNECT;
        }
    }
    else if (0U != p_state->size)
    {
        result = write_packet(p_state, now_ms);
    }
    else if (NETBROKER_ACK == p_state->state)
    {
        result = read_packet(p_state, now_ms);
    }
    else
    {
        result = read_packet(p_state, now_ms);
        if ((0 <= result) && (0U == p_state->received))
        {
            if ((!p_state->b_ping) && ((now_ms - p_state->sent_ms) >= 15000U))
            {
                p_state->transmit[0] = 0xc0U;
                p_state->transmit[1] = 0U;
                p_state->size        = 2U;
                p_state->b_ping      = true;
                p_state->ping_ms     = now_ms;
            }
            else if (!p_state->b_ping)
            {
                const int taken      = netmqtt_take(&p_state->mailbox,
                                               generation,
                                               p_state->transmit,
                                               sizeof(p_state->transmit),
                                               &p_state->size);
                p_state->b_telemetry = 0 == taken;
                if (0 > taken)
                {
                    result = -1;
                }
            }
            else
            {
                /* Await the single outstanding PINGRESP before more data. */
            }
            if (0U != p_state->size)
            {
                p_state->deadline_ms = now_ms + 10000U;
            }
        }
    }
    return (0 > result) ? fail(p_state, now_ms, jitter_ms, true) : 0;
}

/** @brief Offline clients retain no history to replay after reconnect. */
int netbroker_offer (netbroker_t *   p_state,
                     const char *    p_topic,
                     const uint8_t * p_value,
                     size_t          size,
                     bool            b_retain)
{
    size_t length = text_size(p_topic, UTHOME_MQTT_TOPIC_BYTES, true);
    if ((NULL == p_state) || (!p_state->b_initialized) || (0U == length) ||
        ((NULL == p_value) && (0U != size)) || (UTHOME_MQTT_VALUE_BYTES < size))
    {
        return -1;
    }
    if (NETBROKER_READY != p_state->state)
    {
        discard(p_state, 1U);
        return 1;
    }
    return netmqtt_offer(&p_state->mailbox,
                         p_topic,
                         length,
                         p_value,
                         size,
                         p_state->generation,
                         b_retain);
}

/** @brief Report whether an offer would start a new packet, not replace one. */
bool netbroker_idle (const netbroker_t * p_state)
{
    return (NULL != p_state) && (p_state->b_initialized) &&
           (NETBROKER_READY == p_state->state) && (0U == p_state->size) &&
           (!p_state->mailbox.b_pending) && (!p_state->b_ping);
}

/** @brief Copy diagnostics without consuming pending data or resetting
 * counters. */
int netbroker_snapshot (const netbroker_t * p_state, netbroker_view_t * p_view)
{
    if ((NULL == p_state) || (NULL == p_view))
    {
        return -1;
    }
    *p_view = (netbroker_view_t){.state = NETBROKER_UNAVAILABLE};
    if (p_state->b_initialized)
    {
        p_view->state     = (uint32_t)p_state->state;
        p_view->failures  = p_state->failures;
        p_view->replaced  = p_state->mailbox.replaced;
        p_view->discarded = p_state->discarded;
    }
    return 0;
}

/** @brief Count local loss without wrap or implying remote delivery status. */
static void discard (netbroker_t * p_state, uint32_t count)
{
    p_state->discarded = ((UINT32_MAX - p_state->discarded) < count)
                             ? UINT32_MAX
                             : p_state->discarded + count;
}

/** @brief Require bounded printable text, without MQTT topic wildcards. */
static size_t text_size (const char * p_text, size_t maximum, bool b_topic)
{
    if (NULL == p_text)
    {
        return 0U;
    }
    for (size_t index = 0U; index <= maximum; ++index)
    {
        const unsigned char value = (unsigned char)p_text[index];
        if (0U == value)
        {
            return index;
        }
        if ((0x21U > value) || (0x7eU < value) ||
            ((b_topic) && (('+' == value) || ('#' == value))))
        {
            return 0U;
        }
    }
    return 0U;
}

/** @brief Length-prefixed MQTT UTF-8 subset, after complete input validation.
 */
static void field (uint8_t * p_data, size_t * p_offset, const char * p_text)
{
    const size_t length    = strlen(p_text);
    p_data[*p_offset]      = (uint8_t)(length >> 8U);
    p_data[*p_offset + 1U] = (uint8_t)(length & 0xffU);
    memcpy(&p_data[*p_offset + 2U], p_text, length);
    *p_offset += length + 2U;
}

/** @brief All validated fields fit below 400 bytes of existing TX storage. */
static void connect_packet (netbroker_t * p_state)
{
    uint8_t * p_data    = p_state->transmit;
    size_t    offset    = 3U;
    size_t    remaining = 0U;
    field(p_data, &offset, "MQTT");
    p_data[offset++] = 4U;
    p_data[offset++] = 0xe6U;
    p_data[offset++] = 0U;
    p_data[offset++] = 30U;
    field(p_data, &offset, p_state->config.p_client);
    field(p_data, &offset, p_state->config.p_availability);
    field(p_data, &offset, "offline");
    field(p_data, &offset, p_state->config.p_user);
    field(p_data, &offset, p_state->config.p_password);
    remaining = offset - 3U;
    p_data[0] = 0x10U;
    if (128U > remaining)
    {
        memmove(&p_data[2], &p_data[3], remaining);
        p_data[1] = (uint8_t)remaining;
        --offset;
    }
    else
    {
        p_data[1] = (uint8_t)((remaining % 128U) | 0x80U);
        p_data[2] = (uint8_t)(remaining / 128U);
    }
    p_state->size = offset;
}

/** @brief Clear session buffers before scheduling the next bounded attempt. */
static int fail (netbroker_t * p_state,
                 uint64_t      now_ms,
                 uint32_t      jitter_ms,
                 bool          b_failure)
{
    p_state->config.p_close(p_state->config.p_context);
    memset(p_state->transmit, 0, sizeof(p_state->transmit));
    memset(p_state->receive, 0, sizeof(p_state->receive));
    discard(p_state,
            (p_state->b_telemetry ? 1U : 0U) +
                (p_state->mailbox.b_pending ? 1U : 0U));
    const uint32_t replaced = p_state->mailbox.replaced;
    p_state->mailbox        = (netmqtt_t){.replaced = replaced};
    p_state->b_telemetry    = false;
    p_state->size           = 0U;
    p_state->offset         = 0U;
    p_state->received       = 0U;
    p_state->b_ping         = false;
    p_state->state          = NETBROKER_OFF;
    if ((b_failure) && (UINT32_MAX > p_state->failures))
    {
        ++p_state->failures;
    }
    /* A timed-out poll may already have scheduled the next retry. */
    if (NETRETRY_WAITING != p_state->retry.state)
    {
        (void)netretry_result(&p_state->retry, now_ms, false, jitter_ms);
    }
    return -1;
}

/** @brief Partial writes retain immutable bytes and one absolute deadline. */
static int write_packet (netbroker_t * p_state, uint64_t now_ms)
{
    const size_t remaining = p_state->size - p_state->offset;
    const int    written =
        p_state->config.p_write(p_state->config.p_context,
                                &p_state->transmit[p_state->offset],
                                remaining);
    if ((0 > written) || ((size_t)written > remaining))
    {
        return -1;
    }
    p_state->offset += (size_t)written;
    if (p_state->offset == p_state->size)
    {
        memset(p_state->transmit, 0, p_state->size);
        p_state->size        = 0U;
        p_state->offset      = 0U;
        p_state->sent_ms     = now_ms;
        p_state->b_telemetry = false;
        if (NETBROKER_CONNECT == p_state->state)
        {
            p_state->state = NETBROKER_ACK;
        }
        else if (NETBROKER_AVAILABLE == p_state->state)
        {
            p_state->state = NETBROKER_READY;
        }
        else
        {
            /* Ordinary PUBLISH/PINGREQ leaves the online state unchanged. */
        }
    }
    return 0;
}

/** @brief Only CONNACK and solicited PINGRESP are legal on this publisher. */
static int read_packet (netbroker_t * p_state, uint64_t now_ms)
{
    const size_t expected  = (NETBROKER_ACK == p_state->state) ? 4U : 2U;
    const size_t remaining = expected - p_state->received;
    const int    count =
        p_state->config.p_read(p_state->config.p_context,
                               &p_state->receive[p_state->received],
                               remaining);
    if ((0 > count) || ((size_t)count > remaining))
    {
        return -1;
    }
    if ((2U == expected) && (0 < count) && (!p_state->b_ping))
    {
        return -1;
    }
    p_state->received += (size_t)count;
    if ((0U != p_state->received) &&
        (p_state->receive[0] != ((4U == expected) ? 0x20U : 0xd0U)))
    {
        return -1;
    }
    if (expected != p_state->received)
    {
        return 0;
    }
    if (4U == expected)
    {
        if ((2U != p_state->receive[1]) || (0U != p_state->receive[2]) ||
            (0U != p_state->receive[3]) ||
            (0 != netretry_result(&p_state->retry, now_ms, true, 0U)))
        {
            return -1;
        }
        (void)netmqtt_offer(&p_state->mailbox,
                            p_state->config.p_availability,
                            strlen(p_state->config.p_availability),
                            (const uint8_t *)"online",
                            6U,
                            p_state->generation,
                            true);
        (void)netmqtt_take(&p_state->mailbox,
                           p_state->generation,
                           p_state->transmit,
                           sizeof(p_state->transmit),
                           &p_state->size);
        p_state->state       = NETBROKER_AVAILABLE;
        p_state->deadline_ms = now_ms + 10000U;
    }
    else if ((!p_state->b_ping) || (0U != p_state->receive[1]))
    {
        return -1;
    }
    else
    {
        p_state->b_ping = false;
    }
    p_state->received = 0U;
    return 0;
}

/* End of file. */

