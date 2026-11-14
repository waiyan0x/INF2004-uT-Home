/**
 * @file txjob.c
 * @brief Validate copied rules and serialize bounded generic transactions.
 */
#include "uthome/txjob.h"
#include <string.h>

static uint32_t word (const uint8_t * p_data);
static void put_word (uint8_t * p_data, uint32_t value);
static uint64_t wide (const uint8_t * p_data);
static void put_wide (uint8_t * p_data, uint64_t value);
static bool envelope (const uint8_t * p_data, size_t size);
static void reply_header (const txjob_t * p_job,
                          const uint8_t * p_request,
                          uint32_t        generation,
                          uint8_t *       p_reply);
static bool name_valid (const uint8_t * p_name);
static bool zero_bytes (const uint8_t * p_data, size_t size);
static txjob_status_t decode (const uint8_t * p_data, txjob_rule_t * p_rule);
static void finish (txjob_t *       p_job,
                    const uint8_t * p_request,
                    txjob_status_t  status,
                    bool            b_cache,
                    uint64_t        now_ms);

/** @brief Copy immutable callbacks before accepting any request. */
int txjob_init (txjob_t *           p_job,
                const txjob_ops_t * p_operations,
                uint64_t            boot_id)
{
    if ((NULL == p_job) || (NULL == p_operations) ||
        (NULL == p_operations->p_begin) || (NULL == p_operations->p_poll) ||
        (NULL == p_operations->p_close) || (0U == boot_id))
    {
        return -1;
    }
    *p_job = (txjob_t){.operations = *p_operations, .boot_id = boot_id};
    return 0;
}

/** @brief Observe the session without I/O, cache changes or ID consumption. */
int txjob_query (const txjob_t * p_job,
                 const uint8_t * p_request,
                 size_t          size,
                 uint32_t        generation,
                 bool            b_idle,
                 uint64_t        now_ms,
                 uint8_t *       p_reply)
{
    if ((NULL == p_job) || (0U == p_job->boot_id) || (NULL == p_reply) ||
        (!envelope(p_request, size)) || (3U != p_request[3]) ||
        (!zero_bytes(&p_request[16], 80U)))
    {
        return -1;
    }
    reply_header(p_job, p_request, generation, p_reply);
    if ((0U != wide(&p_request[8])) && (wide(&p_request[8]) != p_job->boot_id))
    {
        p_reply[41] = TXJOB_MISMATCH;
    }
    put_word(&p_reply[44], p_job->last_id);
    if (0U != p_job->last_id)
    {
        put_wide(&p_reply[48], wide(&p_job->cached[76]));
        p_reply[61] = p_job->cached[3];
        p_reply[62] = p_job->cached[41];
        put_word(&p_reply[68], word(&p_job->cached[16]));
    }
    if (p_job->b_busy)
    {
        put_word(&p_reply[56], word(&p_job->request[4]));
        p_reply[60] |= 2U;
    }
    if (p_job->b_closing)
    {
        p_reply[60] |= 4U;
    }
    if ((p_job->b_loaded) && (generation == p_job->generation))
    {
        p_reply[60] |= 1U;
        p_reply[63] = p_job->rule.write_size;
        p_reply[64] = p_job->rule.read_size;
    }
    if (b_idle && (!p_job->b_busy))
    {
        p_reply[60] |= 8U;
    }
    put_wide(&p_reply[72], now_ms);
    return 0;
}

/** @brief Admission never repeats a completed driver call for an exact ID. */
int txjob_submit (txjob_t *       p_job,
                  const uint8_t * p_request,
                  size_t          size,
                  uint32_t        generation,
                  bool            b_idle,
                  uint64_t        now_ms)
{
    if ((NULL == p_job) || (NULL == p_job->operations.p_begin) ||
        (p_job->b_busy) || (p_job->b_reply))
    {
        return -1;
    }
    if (generation != p_job->generation)
    {
        p_job->generation = generation;
        p_job->b_loaded   = false;
        p_job->rule       = (txjob_rule_t){0};
    }
    p_job->written  = 0U;
    p_job->received = 0U;
    memset(p_job->data, 0, sizeof(p_job->data));
    if ((!envelope(p_request, size)) ||
        ((1U != p_request[3]) && (2U != p_request[3])) ||
        (!zero_bytes(&p_request[86], 10U)))
    {
        finish(p_job, NULL, TXJOB_INVALID, false, now_ms);
        return 0;
    }
    if ((wide(&p_request[8]) != p_job->boot_id) ||
        (word(&p_request[16]) != generation))
    {
        finish(p_job, p_request, TXJOB_MISMATCH, false, now_ms);
        return 0;
    }
    const uint32_t request_id = word(&p_request[4]);
    if (request_id <= p_job->last_id)
    {
        if ((request_id == p_job->last_id) &&
            (0 == memcmp(p_request, p_job->request, TXJOB_WIRE)))
        {
            memcpy(p_job->reply, p_job->cached, TXJOB_WIRE);
            p_job->b_reply = true;
        }
        else
        {
            finish(p_job, p_request, TXJOB_STALE, false, now_ms);
        }
        return 0;
    }
    memcpy(p_job->request, p_request, TXJOB_WIRE);
    if ((!b_idle) || (now_ms > UINT64_MAX - 1000U))
    {
        finish(p_job, p_request, TXJOB_BUSY, true, now_ms);
        return 0;
    }
    if (1U == p_request[3])
    {
        txjob_rule_t         candidate = {0};
        const txjob_status_t status    = decode(p_request, &candidate);
        if (TXJOB_OK == status)
        {
            p_job->rule     = candidate;
            p_job->b_loaded = true;
        }
        finish(p_job, p_request, status, true, now_ms);
        return 0;
    }
    if ((2U != p_request[3]) || (!name_valid(&p_request[24])) ||
        (!zero_bytes(&p_request[40], 56U)))
    {
        finish(p_job, p_request, TXJOB_INVALID, true, now_ms);
        return 0;
    }
    if (!p_job->b_loaded)
    {
        finish(p_job, p_request, TXJOB_UNCONFIGURED, true, now_ms);
        return 0;
    }
    if ((word(&p_request[20]) != p_job->rule.revision) ||
        (0 != memcmp(&p_request[24], p_job->rule.device, 16U)))
    {
        finish(p_job, p_request, TXJOB_MISMATCH, true, now_ms);
        return 0;
    }
    p_job->last_ms     = now_ms;
    p_job->deadline_ms = now_ms + p_job->rule.timeout_ms;
    p_job->b_busy      = true;
    p_job->status      = p_job->operations.p_begin(
        p_job->operations.p_context, &p_job->rule, now_ms);
    /* Even failed acquisition may retain a claim, so close must run. */
    p_job->b_closing = TXJOB_OK != p_job->status;
    if (p_job->b_closing)
    {
        txjob_step(p_job, now_ms);
    }
    return 0;
}

/** @brief Advance one driver unit; retain all ownership after failed cleanup.
 */
void txjob_step (txjob_t * p_job, uint64_t now_ms)
{
    if ((NULL == p_job) || (!p_job->b_busy))
    {
        return;
    }
    if (!p_job->b_closing)
    {
        if ((now_ms < p_job->last_ms) || (now_ms >= p_job->deadline_ms))
        {
            p_job->status =
                (now_ms < p_job->last_ms) ? TXJOB_IO : TXJOB_TIMEOUT;
        }
        else
        {
            p_job->status =
                p_job->operations.p_poll(p_job->operations.p_context,
                                         now_ms,
                                         &p_job->written,
                                         p_job->data,
                                         &p_job->received);
        }
        p_job->last_ms = now_ms;
        if (TXJOB_PENDING == p_job->status)
        {
            return;
        }
        if ((p_job->written > p_job->rule.write_size) ||
            ((TXJOB_OK == p_job->status) &&
             (p_job->received != p_job->rule.read_size)))
        {
            p_job->status  = TXJOB_IO;
            p_job->written = 0U;
        }
        p_job->b_closing = true;
    }
    if (0 != p_job->operations.p_close(p_job->operations.p_context))
    {
        p_job->status = TXJOB_IO;
        return;
    }
    p_job->b_busy    = false;
    p_job->b_closing = false;
    finish(p_job, p_job->request, p_job->status, true, now_ms);
}

/** @brief Cancel uncertain work; never automatically replay transmitted bytes.
 */
void txjob_cancel (txjob_t * p_job)
{
    if ((NULL != p_job) && (p_job->b_busy))
    {
        p_job->status    = TXJOB_CANCELLED;
        p_job->b_closing = true;
    }
}

/** @brief Consume only the response mailbox, keeping the exact-retry cache. */
int txjob_take (txjob_t * p_job, uint8_t * p_reply, size_t capacity)
{
    if ((NULL == p_job) || (NULL == p_reply) || (capacity < TXJOB_WIRE))
    {
        return -1;
    }
    if (!p_job->b_reply)
    {
        return 1;
    }
    memcpy(p_reply, p_job->reply, TXJOB_WIRE);
    p_job->b_reply = false;
    return 0;
}

/** @brief Decode unaligned little-endian values by explicit bytes. */
static uint32_t word (const uint8_t * p_data)
{
    return (uint32_t)p_data[0] | ((uint32_t)p_data[1] << 8U) |
           ((uint32_t)p_data[2] << 16U) | ((uint32_t)p_data[3] << 24U);
}

/** @brief Encode wire values independent of native structure padding. */
static void put_word (uint8_t * p_data, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index)
    {
        p_data[index] = (uint8_t)((value >> (index * 8U)) & 255U);
    }
}

/** @brief Decode a boot identity or timestamp without alignment assumptions. */
static uint64_t wide (const uint8_t * p_data)
{
    return (uint64_t)word(p_data) | ((uint64_t)word(&p_data[4]) << 32U);
}

/** @brief Bound each conversion to the low or high 32 bits. */
static void put_wide (uint8_t * p_data, uint64_t value)
{
    put_word(p_data, (uint32_t)(value & UINT32_MAX));
    put_word(&p_data[4], (uint32_t)(value >> 32U));
}

/** @brief Reject old versions before interpreting the changed byte offsets. */
static bool envelope (const uint8_t * p_data, size_t size)
{
    return (NULL != p_data) && (TXJOB_WIRE == size) && ('T' == p_data[0]) &&
           ('J' == p_data[1]) && (2U == p_data[2]) && (0U != word(&p_data[4]));
}

/** @brief Require canonical identifier bytes and a zero-padded terminator. */
static bool name_valid (const uint8_t * p_name)
{
    bool b_end = false;
    if (0U == p_name[0])
    {
        return false;
    }
    for (size_t index = 0U; index < 16U; ++index)
    {
        const uint8_t value = p_name[index];
        if (0U == value)
        {
            b_end = true;
        }
        else if (b_end || (!(((value >= 'A') && (value <= 'Z')) ||
                             ((value >= 'a') && (value <= 'z')) ||
                             ((value >= '0') && (value <= '9')) ||
                             ('_' == value) || ('-' == value))))
        {
            return false;
        }
        else
        {
            /* This ASCII identifier byte is valid. */
        }
    }
    return b_end;
}

/** @brief Canonical unused bytes prevent hidden instruction differences. */
static bool zero_bytes (const uint8_t * p_data, size_t size)
{
    for (size_t index = 0U; index < size; ++index)
    {
        if (0U != p_data[index])
        {
            return false;
        }
    }
    return true;
}

/** @brief Admit the supported fixed-board transfer shapes before commit. */
static txjob_status_t decode (const uint8_t * p_data, txjob_rule_t * p_rule)
{
    if ((!name_valid(&p_data[24])) || (0U == word(&p_data[20])) ||
        (p_data[50] > TXJOB_TX) || (p_data[51] > TXJOB_RX) ||
        ((0U == p_data[50]) && (0U == p_data[51])) ||
        (!zero_bytes(&p_data[54], 2U)))
    {
        return TXJOB_INVALID;
    }
    const uint16_t timeout =
        (uint16_t)((uint16_t)p_data[48] | ((uint16_t)p_data[49] << 8U));
    if ((timeout < 10U) || (timeout > 1000U) ||
        (!zero_bytes(&p_data[56U + p_data[50]], TXJOB_TX - p_data[50])))
    {
        return TXJOB_INVALID;
    }
    *p_rule = (txjob_rule_t){.revision   = word(&p_data[20]),
                             .rate       = word(&p_data[44]),
                             .timeout_ms = timeout,
                             .protocol   = p_data[40],
                             .address    = p_data[41],
                             .mode       = p_data[42],
                             .dc_pin     = p_data[43],
                             .write_size = p_data[50],
                             .read_size  = p_data[51],
                             .cs_pin     = p_data[52],
                             .dc_level   = p_data[53]};
    memcpy(p_rule->device, &p_data[24], 16U);
    memcpy(p_rule->write, &p_data[56], TXJOB_TX);
    if (2U == p_rule->protocol)
    {
        const bool b_cs = (3U == p_rule->cs_pin) || (4U == p_rule->cs_pin) ||
                          (5U == p_rule->cs_pin) || (10U == p_rule->cs_pin);
        const bool b_dc = (3U == p_rule->dc_pin) || (4U == p_rule->dc_pin) ||
                          (5U == p_rule->dc_pin) || (10U == p_rule->dc_pin);
        if ((p_rule->rate < 100000U) || (p_rule->rate > 8000000U) ||
            (p_rule->mode > 3U) || (!b_cs) || (p_rule->address != 0U) ||
            ((!b_dc) && (p_rule->dc_pin != 255U)) ||
            (p_rule->dc_pin == p_rule->cs_pin) || (p_rule->dc_level > 1U) ||
            ((255U == p_rule->dc_pin) && (p_rule->dc_level != 0U)))
        {
            return TXJOB_INVALID;
        }
        return (0U == p_rule->read_size) ? TXJOB_OK : TXJOB_UNSUPPORTED;
    }
    if ((p_rule->mode != 0U) || (p_rule->dc_pin != 255U) ||
        (p_rule->cs_pin != 255U) || (p_rule->dc_level != 0U))
    {
        return TXJOB_INVALID;
    }
    if (1U == p_rule->protocol)
    {
        return ((p_rule->rate >= 10000U) && (p_rule->rate <= 400000U) &&
                (p_rule->address >= 8U) && (p_rule->address <= 119U))
                   ? TXJOB_OK
                   : TXJOB_INVALID;
    }
    if (3U == p_rule->protocol)
    {
        return ((p_rule->rate >= 9600U) && (p_rule->rate <= 460800U) &&
                (0U == p_rule->address))
                   ? TXJOB_OK
                   : TXJOB_INVALID;
    }
    return TXJOB_UNSUPPORTED;
}

/** @brief Populate copied identity and selected settings without sample bytes.
 */
static void reply_header (const txjob_t * p_job,
                          const uint8_t * p_request,
                          uint32_t        generation,
                          uint8_t *       p_reply)
{
    memset(p_reply, 0, TXJOB_WIRE);
    p_reply[0] = 'T';
    p_reply[1] = 'J';
    p_reply[2] = 2U;
    if (NULL != p_request)
    {
        p_reply[3] = p_request[3];
        put_word(&p_reply[4], word(&p_request[4]));
    }
    put_wide(&p_reply[8], p_job->boot_id);
    put_word(&p_reply[16], generation);
    if ((p_job->b_loaded) && (generation == p_job->generation))
    {
        put_word(&p_reply[20], p_job->rule.revision);
        memcpy(&p_reply[24], p_job->rule.device, 16U);
        p_reply[40] = p_job->rule.protocol;
        p_reply[84] = p_job->rule.address;
        p_reply[85] = p_job->rule.mode;
        p_reply[86] = p_job->rule.cs_pin;
        p_reply[87] = p_job->rule.dc_pin;
        put_word(&p_reply[88], p_job->rule.rate);
        p_reply[92] = (uint8_t)(p_job->rule.timeout_ms & 255U);
        p_reply[93] = (uint8_t)(p_job->rule.timeout_ms >> 8U);
        p_reply[94] = p_job->rule.dc_level;
    }
}

/** @brief Publish a completion; exact retries retain the original time/bytes.
 */
static void finish (txjob_t *       p_job,
                    const uint8_t * p_request,
                    txjob_status_t  status,
                    bool            b_cache,
                    uint64_t        now_ms)
{
    uint8_t * p_reply = p_job->reply;
    reply_header(p_job, p_request, p_job->generation, p_reply);
    p_reply[41] = (uint8_t)status;
    p_reply[42] = p_job->written;
    if ((TXJOB_OK == status) && (p_job->received <= TXJOB_RX))
    {
        p_reply[43] = p_job->received;
        memcpy(&p_reply[44], p_job->data, p_job->received);
    }
    put_wide(&p_reply[76], now_ms);
    p_job->b_reply = true;
    if (b_cache)
    {
        p_job->last_id = word(&p_request[4]);
        memcpy(p_job->cached, p_reply, TXJOB_WIRE);
    }
}

/* End of file. */

