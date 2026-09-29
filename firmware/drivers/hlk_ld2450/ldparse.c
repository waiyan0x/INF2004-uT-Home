/**
 * @file ldparse.c
 * @brief Adapt member 3's sliding parser to typed millisecond samples.
 * @details Origin: kimNewpsysk1t, commit 974eb9c; see INTEGRATION.md.
 */
#include "ldparse.h"
#include <string.h>
static uint16_t little_word (const uint8_t * p_bytes);
static int16_t signed_value (const uint8_t * p_bytes);
static void discard_byte (ldparse_t * p_parser);
static const uint8_t g_header[4] = {0xAAU, 0xFFU, 0x03U, 0x00U};

/** @brief Recover prefixes and overlapping frames with at most 30 shifts. */
bool ldparse_feed (ldparse_t *              p_parser,
                   uint8_t                  byte,
                   uint64_t                 now_ms,
                   sample_target_report_t * p_report)
{
    if ((NULL == p_parser) || (NULL == p_report) ||
        (p_parser->used >= UTHOME_LD_FRAME_BYTES))
    {
        return false;
    }
    p_parser->bytes[p_parser->used] = byte;
    ++p_parser->used;
    size_t prefix = (p_parser->used < 4U) ? p_parser->used : 4U;
    while ((p_parser->used > 0U) &&
           (0 != memcmp(p_parser->bytes, g_header, prefix)))
    {
        discard_byte(p_parser);
        prefix = (p_parser->used < 4U) ? p_parser->used : 4U;
    }
    if (UTHOME_LD_FRAME_BYTES != p_parser->used)
    {
        return false;
    }
    if ((0x55U != p_parser->bytes[28]) || (0xCCU != p_parser->bytes[29]))
    {
        ++p_parser->framing_errors;
        discard_byte(p_parser);
        return false;
    }
    *p_report                     = (sample_target_report_t){0};
    p_report->header.timestamp_ms = now_ms;
    p_report->header.status       = UTHOME_SAMPLE_OK;
    p_report->header.source_index = UTHOME_SAMPLE_NO_SOURCE;
    for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
    {
        const uint8_t *   p_bytes  = &p_parser->bytes[4U + (index * 8U)];
        sample_target_t * p_target = &p_report->targets[index];
        p_target->x_mm             = signed_value(p_bytes);
        p_target->y_mm             = signed_value(&p_bytes[2]);
        p_target->speed_cm_s       = signed_value(&p_bytes[4]);
        p_target->resolution_mm    = little_word(&p_bytes[6]);
        for (size_t offset = 0U; offset < 8U; ++offset)
        {
            p_target->b_present =
                (p_target->b_present) || (0U != p_bytes[offset]);
        }
    }
    ++p_parser->frames;
    p_parser->used = 0U;
    return true;
}

/** @brief Invalidate an incomplete report without losing diagnostics. */
void ldparse_reset (ldparse_t * p_parser)
{
    if (NULL != p_parser)
    {
        p_parser->used = 0U;
    }
}

/** @brief Decode byte order explicitly without aliasing or alignment casts. */
static uint16_t little_word (const uint8_t * p_bytes)
{
    /* Widen before shifting so integer promotion cannot make operands signed.
     * The assembled value is at most UINT16_MAX. */
    return (uint16_t)((uint32_t)p_bytes[0] | ((uint32_t)p_bytes[1] << 8U));
}

/** @brief Bit 15 means positive; magnitude is always within INT16_MAX. */
static int16_t signed_value (const uint8_t * p_bytes)
{
    const uint32_t word      = little_word(p_bytes);
    const int32_t  magnitude = word & UINT16_C(0x7FFF);
    /* Both signs fit int16_t, including either encoding of zero. */
    return (int16_t)((0U != (word & UINT16_C(0x8000))) ? magnitude
                                                       : -magnitude);
}

/** @brief Shift only initialized bytes; the caller guarantees used > 0. */
static void discard_byte (ldparse_t * p_parser)
{
    --p_parser->used;
    memmove(p_parser->bytes, &p_parser->bytes[1], p_parser->used);
    ++p_parser->discarded_bytes;
}

/* End of file. */

