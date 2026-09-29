/** @file oledview.c
 * @brief Draw a fixed text page using the driver's existing pixel storage. */
#include "oledview.h"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static const char g_characters[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-._/:";
static const uint8_t g_glyphs[][5] = {
    {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}, /* 0 */
    {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}, /* 1 */
    {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}, /* 2 */
    {0x41U, 0x49U, 0x49U, 0x49U, 0x36U}, /* 3 */
    {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}, /* 4 */
    {0x4FU, 0x49U, 0x49U, 0x49U, 0x31U}, /* 5 */
    {0x3EU, 0x49U, 0x49U, 0x49U, 0x30U}, /* 6 */
    {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}, /* 7 */
    {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}, /* 8 */
    {0x06U, 0x49U, 0x49U, 0x49U, 0x3EU}, /* 9 */
    {0x7EU, 0x09U, 0x09U, 0x09U, 0x7EU}, /* A */
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}, /* B */
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x41U}, /* C */
    {0x7FU, 0x41U, 0x41U, 0x41U, 0x3EU}, /* D */
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}, /* E */
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}, /* F */
    {0x3EU, 0x41U, 0x49U, 0x49U, 0x79U}, /* G */
    {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}, /* H */
    {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}, /* I */
    {0x30U, 0x40U, 0x41U, 0x3FU, 0x01U}, /* J */
    {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}, /* K */
    {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}, /* L */
    {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU}, /* M */
    {0x7FU, 0x02U, 0x04U, 0x08U, 0x7FU}, /* N */
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}, /* O */
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}, /* P */
    {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU}, /* Q */
    {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}, /* R */
    {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}, /* S */
    {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}, /* T */
    {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}, /* U */
    {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}, /* V */
    {0x7FU, 0x20U, 0x18U, 0x20U, 0x7FU}, /* W */
    {0x63U, 0x14U, 0x08U, 0x14U, 0x63U}, /* X */
    {0x03U, 0x04U, 0x78U, 0x04U, 0x03U}, /* Y */
    {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}, /* Z */
    {0x08U, 0x08U, 0x08U, 0x08U, 0x08U}, /* - */
    {0x00U, 0x00U, 0x60U, 0x00U, 0x00U}, /* . */
    {0x40U, 0x40U, 0x40U, 0x40U, 0x40U}, /* _ */
    {0x40U, 0x30U, 0x08U, 0x06U, 0x01U}, /* / */
    {0x00U, 0x00U, 0x36U, 0x00U, 0x00U}, /* : */
};

static void line (ssd1306_t * p_display, size_t row, const char * p_text);
static void values (ssd1306_t * p_display, const sampcache_entry_t * p_entry);
static void environment (ssd1306_t *                  p_display,
                         const sample_environment_t * p_sample);
static void targets (ssd1306_t *                    p_display,
                     const sample_target_report_t * p_sample);

/** @brief Clear obsolete pixels and draw identity, quality and typed values. */
int oledview_render (ssd1306_t *               p_display,
                     const sampcache_entry_t * p_entry,
                     uint64_t                  now_ms)
{
    if ((NULL == p_display) || (64U != p_display->config.height) ||
        (0 != ssd1306_clear(p_display)))
    {
        return SPIBUS_STATE;
    }
    if ((NULL == p_entry) || (!p_entry->b_expected))
    {
        line(p_display, 0U, "WAITING FOR DATA");
        return 0;
    }
    char       text[22] = {0};
    const bool b_health =
        UTHOME_SAMPLE_NO_SOURCE == p_entry->source.source_index;
    /* Queue/source IDs fit unsigned. A 17-character label is deliberate;
     * its prefixed D-index or H-queue retains unambiguous source identity. */
    (void)snprintf(text,
                   sizeof(text),
                   "%c%u %.17s",
                   b_health ? 'H' : 'D',
                   b_health ? (unsigned)p_entry->source.h_queue
                            : (unsigned)p_entry->source.source_index,
                   p_entry->source.name);
    line(p_display, 0U, text);
    const uint32_t     quality     = sampcache_quality(p_entry, now_ms);
    const char * const p_quality[] = {"MISSING", "FRESH", "STALE", "FAULT"};
    line(p_display, 1U, p_quality[quality]);
    if ((1U == quality) || (2U == quality))
    {
        values(p_display, p_entry);
    }
    else if (3U == quality)
    {
        line(p_display, 2U, "READING UNAVAILABLE");
    }
    else
    {
        /* Missing data has no numeric value, including no invented zero. */
    }
    return 0;
}

/** @brief Draw five columns per glyph directly into the page-major buffer. */
static void line (ssd1306_t * p_display, size_t row, const char * p_text)
{
    for (size_t index = 0U; (index < 21U) && ('\0' != p_text[index]); ++index)
    {
        char character = p_text[index];
        if ((character >= 'a') && (character <= 'z'))
        {
            /* The ASCII alphabet difference maps within the char range. */
            character = (char)(character - 'a' + 'A');
        }
        const char * p_glyph = strchr(g_characters, character);
        if (NULL != p_glyph)
        {
            /* Both pointers belong to g_characters; the difference is 0..40. */
            const size_t glyph = (size_t)(p_glyph - g_characters);
            memcpy(&p_display->frame[(row * 128U) + (index * 6U)],
                   g_glyphs[glyph],
                   5U);
        }
    }
}

/** @brief Display integers without enabling floating-point printf support. */
static void values (ssd1306_t * p_display, const sampcache_entry_t * p_entry)
{
    char text[32] = {0};
    if (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type)
    {
        environment(p_display, &p_entry->value.environment);
    }
    else if (UTHOME_SAMPLE_TARGETS == p_entry->source.type)
    {
        targets(p_display, &p_entry->value.targets);
    }
    else if (UTHOME_SAMPLE_DISTANCE == p_entry->source.type)
    {
        /* uint32 values use the matching printf macro; fixed labels fit 21
         * screen columns. snprintf return is unneeded: every destination is
         * bounded and sized.
         */
        (void)snprintf(text,
                       sizeof(text),
                       "DIST %" PRIu32 " MM",
                       p_entry->value.distance.distance_mm);
        line(p_display, 2U, text);
        (void)snprintf(text,
                       sizeof(text),
                       "ECHO %" PRIu32 " US",
                       p_entry->value.distance.echo_us);
        line(p_display, 3U, text);
    }
    else
    {
        /* The cache admits only these four types and eight health event codes.
         */
        (void)snprintf(text,
                       sizeof(text),
                       "EVENT %u",
                       (unsigned)p_entry->value.health.event);
        line(p_display, 2U, text);
        (void)snprintf(text,
                       sizeof(text),
                       "COUNT %" PRIu32,
                       p_entry->value.health.occurrences);
        line(p_display, 3U, text);
        line(p_display, 4U, "LAST EVENT REPORT");
    }
}

/** @brief Bound conversions, retaining negative sub-degree temperatures. */
static void environment (ssd1306_t *                  p_display,
                         const sample_environment_t * p_sample)
{
    if ((!isfinite(p_sample->pressure_pa)) ||
        (!isfinite(p_sample->temperature_c)) ||
        (p_sample->pressure_pa < -999999.0F) ||
        (p_sample->pressure_pa > 999999.0F) ||
        (p_sample->temperature_c < -9999.0F) ||
        (p_sample->temperature_c > 9999.0F))
    {
        line(p_display, 2U, "OUT OF DISPLAY RANGE");
        return;
    }
    /* Checked finite ranges fit int32 even after scaling temperature by ten.
     * Truncation is a presentation choice; telemetry retains original floats.
     */
    const int32_t pressure  = (int32_t)p_sample->pressure_pa;
    const int32_t tenths    = (int32_t)(p_sample->temperature_c * 10.0F);
    const int32_t magnitude = (tenths < 0) ? -tenths : tenths;
    char          text[32]  = {0};
    /* Both formats fit this bounded buffer; line clips to 21 screen columns. */
    (void)snprintf(text, sizeof(text), "P %" PRId32 " PA", pressure);
    line(p_display, 2U, text);
    (void)snprintf(text,
                   sizeof(text),
                   "T %s%" PRId32 ".%" PRId32 " C",
                   (tenths < 0) ? "-" : "",
                   magnitude / 10,
                   magnitude % 10);
    line(p_display, 3U, text);
}

/** @brief Keep all three target slots and their signed coordinates visible. */
static void targets (ssd1306_t *                    p_display,
                     const sample_target_report_t * p_sample)
{
    for (unsigned index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
    {
        const sample_target_t * p_target = &p_sample->targets[index];
        char                    text[40] = {0};
        /* int16 fields fit int; uint16 resolution fits unsigned. These bounded
         * text formats require no allocation; unused rows remain blank. */
        if (p_target->b_present)
        {
            (void)snprintf(text,
                           sizeof(text),
                           "%u X%d Y%d MM",
                           index + 1U,
                           (int)p_target->x_mm,
                           (int)p_target->y_mm);
            line(p_display, 2U + (index * 2U), text);
            (void)snprintf(text,
                           sizeof(text),
                           "V%dCM/S R%uMM",
                           (int)p_target->speed_cm_s,
                           (unsigned)p_target->resolution_mm);
            line(p_display, 3U + (index * 2U), text);
        }
        else
        {
            (void)snprintf(text, sizeof(text), "%u ABSENT", index + 1U);
            line(p_display, 2U + (index * 2U), text);
        }
    }
}

/* End of file. */

