/**
 * @file txwire.c
 * @brief Decode explicit wire fields, dispatch a rule, and encode a result.
 */
#include "txwire.h"
#include <string.h>

static uint32_t read_u32 (const uint8_t * p_bytes);
static void write_u32 (uint8_t * p_bytes, uint32_t value);

/** @brief Handle exactly one update or read; unknown input has no side effects.
 */
void txwire_process (txrule_t *      p_state,
                     const uint8_t * p_request,
                     size_t          size,
                     txrule_read_t * p_read,
                     void *          p_context,
                     uint8_t *       p_reply)
{
    txrule_result_t result     = {.status = TXRULE_INVALID};
    uint32_t        request_id = 0U;
    uint8_t         kind       = 0U;
    if (NULL == p_reply)
    {
        return;
    }
    if ((NULL != p_state) && (NULL != p_request) &&
        (TXWIRE_REQUEST_SIZE == size) && ('T' == p_request[0]) &&
        ('R' == p_request[1]) && (1U == p_request[2]) && (0U == p_request[47]))
    {
        txrule_config_t candidate = {.version = 1U};
        kind                      = p_request[3];
        request_id                = read_u32(&p_request[4]);
        candidate.revision        = read_u32(&p_request[8]);
        memcpy(candidate.device, &p_request[12], TXRULE_NAME_SIZE);
        candidate.address   = p_request[44];
        candidate.reg       = p_request[45];
        candidate.read_size = p_request[46];
        if (1U == kind)
        {
            result.status = txrule_load(p_state, &candidate);
        }
        else if ((2U == kind) && (0U == candidate.address) &&
                 (0U == candidate.reg) && (0U == candidate.read_size))
        {
            txrule_execute(p_state,
                           candidate.device,
                           candidate.revision,
                           p_read,
                           p_context,
                           &result);
        }
        else
        {
            /* Unsupported operation or nonzero reserved fields: no I/O. */
        }
    }
    if ((NULL != p_state) && (p_state->b_loaded))
    {
        result.rule = p_state->active;
    }
    memset(p_reply, 0, TXWIRE_REPLY_SIZE);
    p_reply[0] = 'T';
    p_reply[1] = 'R';
    p_reply[2] = 1U;
    p_reply[3] = kind;
    write_u32(&p_reply[4], request_id);
    write_u32(&p_reply[8], result.rule.revision);
    memcpy(&p_reply[12], result.rule.device, TXRULE_NAME_SIZE);
    /* All active rule fields were range-checked before committing. */
    p_reply[44] = (uint8_t)result.rule.address;
    p_reply[45] = (uint8_t)result.rule.reg;
    p_reply[46] = (uint8_t)result.rule.read_size;
    p_reply[47] = (uint8_t)result.status;
    p_reply[48] = result.size;
    memcpy(&p_reply[49], result.data, TXRULE_DATA_SIZE);
}

/** @brief Decode little-endian integers without alignment assumptions. */
static uint32_t read_u32 (const uint8_t * p_bytes)
{
    return (uint32_t)p_bytes[0] | ((uint32_t)p_bytes[1] << 8U) |
           ((uint32_t)p_bytes[2] << 16U) | ((uint32_t)p_bytes[3] << 24U);
}

/** @brief Encode each byte explicitly; native structure padding is not sent. */
static void write_u32 (uint8_t * p_bytes, uint32_t value)
{
    for (size_t idx = 0U; idx < 4U; ++idx)
    {
        p_bytes[idx] = (uint8_t)((value >> (idx * 8U)) & 255U);
    }
}

/* End of file. */

