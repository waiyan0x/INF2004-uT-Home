/**
 * @file cfgwire.c
 * @brief Reject damaged frames before any configuration callback is invoked.
 */
#include "cfgwire.h"
#include <string.h>
static const uint8_t g_magic[4] = {'U', 'T', 'H', '1'};
static int accept_header (cfgwire_t * p_receiver);
static int finish (cfgwire_t * p_receiver);

/** @brief Invalidate a borrowed payload without clearing an entire data buffer.
 */
void cfgwire_reset (cfgwire_t * p_receiver)
{
    if (NULL != p_receiver)
    {
        p_receiver->header_used = 0U;
        p_receiver->used        = 0U;
        p_receiver->expected    = 0U;
        p_receiver->b_ready     = false;
    }
}

/** @brief A fixed whole-frame deadline cannot be extended by trickled bytes. */
int cfgwire_expire (cfgwire_t * p_receiver, uint64_t now_ms)
{
    if (NULL == p_receiver)
    {
        return -2;
    }
    if ((0U != p_receiver->header_used) && (!p_receiver->b_ready) &&
        ((now_ms < p_receiver->last_ms) ||
         ((now_ms - p_receiver->start_ms) >= UTHOME_CFGWIRE_TIMEOUT)))
    {
        cfgwire_reset(p_receiver);
        return -1;
    }
    p_receiver->last_ms = now_ms;
    return 0;
}

/** @brief Consume one byte; retain a completed request until explicit reset. */
int cfgwire_feed (cfgwire_t * p_receiver, uint8_t byte, uint64_t now_ms)
{
    if ((NULL == p_receiver) || (p_receiver->b_ready))
    {
        return -2;
    }
    if (0 != cfgwire_expire(p_receiver, now_ms))
    {
        return -1;
    }
    if (p_receiver->header_used < sizeof(g_magic))
    {
        if (byte != g_magic[p_receiver->header_used])
        {
            cfgwire_reset(p_receiver);
        }
        if (byte == g_magic[p_receiver->header_used])
        {
            if (0U == p_receiver->header_used)
            {
                p_receiver->start_ms = now_ms;
            }
            p_receiver->header[p_receiver->header_used] = byte;
            ++p_receiver->header_used;
        }
        return 0;
    }
    if (p_receiver->header_used < UTHOME_CFGWIRE_HEADER)
    {
        p_receiver->header[p_receiver->header_used] = byte;
        ++p_receiver->header_used;
        return (UTHOME_CFGWIRE_HEADER == p_receiver->header_used)
                   ? accept_header(p_receiver)
                   : 0;
    }
    p_receiver->data[p_receiver->used] = byte;
    ++p_receiver->used;
    return (p_receiver->used == p_receiver->expected) ? finish(p_receiver) : 0;
}

/** @brief Copy metadata only; payload ownership stays with the receiver. */
int cfgwire_request (const cfgwire_t *   p_receiver,
                     cfgwire_request_t * p_request)
{
    if ((NULL == p_receiver) || (NULL == p_request) || (!p_receiver->b_ready))
    {
        return -2;
    }
    /* accept_header admits command bytes 1..8, matching these enumerators. */
    *p_request =
        (cfgwire_request_t){.request_id = cfgbytes_get(&p_receiver->header[8]),
                            .revision   = cfgbytes_get(&p_receiver->header[12]),
                            .command = (cfgwire_command_t)p_receiver->header[5],
                            .size    = p_receiver->used,
                            .p_data  = p_receiver->data};
    return 0;
}

/** @brief Replies echo command/ID and report state through six fixed fields. */
int cfgwire_reply (const cfgwire_request_t * p_request,
                   const uint32_t *          p_fields,
                   uint8_t *                 p_output)
{
    if ((NULL == p_request) || (NULL == p_fields) || (NULL == p_output) ||
        (0U == p_request->request_id) || (p_request->command < CFGWIRE_APPLY) ||
        (p_request->command > CFGWIRE_PROBE))
    {
        return -2;
    }
    memset(p_output, 0, UTHOME_CFGWIRE_REPLY);
    memcpy(p_output, g_magic, sizeof(g_magic));
    p_output[4] = 1U;
    /* Validated command 1..5 and the response bit fit a byte. */
    p_output[5] = (uint8_t)((uint32_t)p_request->command | UINT32_C(0x80));
    cfgbytes_put(&p_output[8], p_request->request_id);
    cfgbytes_put(&p_output[12], p_request->revision);
    cfgbytes_put(&p_output[16], 24U);
    for (size_t index = 0U; index < 6U; ++index)
    {
        cfgbytes_put(&p_output[24U + (index * 4U)], p_fields[index]);
    }
    uint32_t sum = cfgbytes_crc(UINT32_MAX, p_output, 20U);
    sum          = cfgbytes_crc(sum, &p_output[24], 24U);
    cfgbytes_put(&p_output[20], ~sum);
    return 0;
}

/** @brief Frame a bounded read-only observation without native structure bytes.
 */
int cfgwire_observation (const cfgwire_request_t * p_request,
                         uint8_t *                 p_output,
                         size_t                    capacity,
                         size_t                    size)
{
    if ((NULL == p_request) || (NULL == p_output) ||
        ((CFGWIRE_TELEMETRY != p_request->command) &&
         (CFGWIRE_CAPABILITIES != p_request->command) &&
         (CFGWIRE_WORKCOST != p_request->command) &&
         (CFGWIRE_DISCOVER != p_request->command)) ||
        (0U == p_request->request_id) ||
        ((CFGWIRE_DISCOVER != p_request->command) &&
         ((0U != p_request->revision) || (0U != p_request->size))) ||
        ((CFGWIRE_DISCOVER == p_request->command) &&
         (24U != p_request->size)) ||
        (size > UTHOME_CFGWIRE_PAYLOAD) || (0U == size) ||
        (capacity < (UTHOME_CFGWIRE_HEADER + size)))
    {
        return -2;
    }
    memset(p_output, 0, UTHOME_CFGWIRE_HEADER);
    memcpy(p_output, g_magic, sizeof(g_magic));
    p_output[4] = 1U;
    /* The checked observation commands and response bit fit a byte. */
    p_output[5] = (uint8_t)((uint32_t)p_request->command | UINT32_C(0x80));
    cfgbytes_put(&p_output[8], p_request->request_id);
    cfgbytes_put(&p_output[12], p_request->revision);
    /* The checked payload is bounded to 2048 bytes. */
    cfgbytes_put(&p_output[16], (uint32_t)size);
    uint32_t sum = cfgbytes_crc(UINT32_MAX, p_output, 20U);
    sum          = cfgbytes_crc(sum, &p_output[24], size);
    cfgbytes_put(&p_output[20], ~sum);
    return 0;
}

/** @brief No declared size reaches a buffer write before being bounded. */
static int accept_header (cfgwire_t * p_receiver)
{
    const uint8_t * p_header = p_receiver->header;
    const uint32_t  size     = cfgbytes_get(&p_header[16]);
    const uint32_t  revision = cfgbytes_get(&p_header[12]);
    const uint8_t   command  = p_header[5];
    if ((1U != p_header[4]) || (0U != p_header[6]) || (0U != p_header[7]) ||
        (0U == cfgbytes_get(&p_header[8])) || (command < CFGWIRE_APPLY) ||
        (command > CFGWIRE_WORKCOST) || (size > UTHOME_CONFIG_BYTES) ||
        ((CFGWIRE_APPLY == command) && ((0U == size) || (0U == revision))) ||
        ((CFGWIRE_APPLY != command) && (CFGWIRE_DISCOVER != command) &&
         (0U != size)) ||
        ((CFGWIRE_DISCOVER == command) && (24U != size)) ||
        (((CFGWIRE_QUERY == command) || (CFGWIRE_RECOVER == command) ||
          (CFGWIRE_PROBE == command) || (CFGWIRE_TELEMETRY == command) ||
          (CFGWIRE_CAPABILITIES == command) || (CFGWIRE_WORKCOST == command)) &&
         (0U != revision)) ||
        ((CFGWIRE_SAVE == command) && (0U == revision)))
    {
        cfgwire_reset(p_receiver);
        return -1;
    }
    p_receiver->expected = size;
    return (0U == size) ? finish(p_receiver) : 0;
}

/** @brief Cover metadata and payload in one checksum; never use raw C structs.
 */
static int finish (cfgwire_t * p_receiver)
{
    uint32_t sum = cfgbytes_crc(UINT32_MAX, p_receiver->header, 20U);
    sum          = cfgbytes_crc(sum, p_receiver->data, p_receiver->used);
    if ((~sum) != cfgbytes_get(&p_receiver->header[20]))
    {
        cfgwire_reset(p_receiver);
        return -1;
    }
    p_receiver->b_ready = true;
    return 1;
}

/* End of file. */

