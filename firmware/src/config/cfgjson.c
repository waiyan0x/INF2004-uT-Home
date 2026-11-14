/**
 * @file cfgjson.c
 * @brief Compact JSON views without heap allocation or copied string storage.
 */
#include "cfgjson.h"
#include "jsonscan.h"
#include <string.h>

static cfgjson_value_t g_nodes[UTHOME_JSON_NODES] = {0};
static const uint8_t * gp_data                    = NULL;
static size_t          g_size                     = 0U;
static size_t          g_count                    = 0U;
static size_t          g_steps                    = 0U;
static size_t          g_peak                     = 0U;
static bool            gb_initialized             = false;

static void whitespace (size_t * p_offset);
static bool step (void);
static uint8_t decoded (size_t * p_offset);
static bool text_equal (size_t start, const char * p_text);
static bool keys_equal (size_t left, size_t right);
static bool kind (const cfgjson_value_t * p_value, uint8_t expected);
static size_t first_child (const cfgjson_value_t * p_value);
static bool value (size_t * p_offset, size_t depth, uint16_t key);
static bool container (size_t index, size_t * p_offset, size_t depth);
static bool scalar (size_t * p_offset);
static bool integer (const cfgjson_value_t * p_value, int64_t * p_result);

/** @brief Establish exclusive parser ownership once, with no vendor hooks. */
int cfgjson_init (void)
{
    if (gb_initialized)
    {
        return -1;
    }
    gb_initialized = true;
    return 0;
}

/** @brief Validate lexical bounds and grammar before publishing any view. */
const cfgjson_value_t * cfgjson_open (const uint8_t * p_data, size_t size)
{
    if ((!gb_initialized) || (NULL != gp_data) ||
        (!jsonscan_check(p_data, size)))
    {
        return NULL;
    }
    gp_data       = p_data;
    g_size        = size;
    g_peak        = 0U;
    size_t offset = 0U;
    whitespace(&offset);
    if ((offset == size) || ('{' != p_data[offset]) ||
        (!value(&offset, 0U, UINT16_MAX)))
    {
        cfgjson_close();
        return NULL;
    }
    whitespace(&offset);
    if (offset != size)
    {
        cfgjson_close();
        return NULL;
    }
    return &g_nodes[0];
}

/** @brief Invalidate all borrowed views; retain the last storage high water. */
void cfgjson_close (void)
{
    gp_data = NULL;
    g_size  = 0U;
    g_count = 0U;
    g_steps = 0U;
}

/** @brief Report occupied view bytes, excluding the caller's existing input. */
size_t cfgjson_peak (void)
{
    return g_peak;
}

/** @brief Find an exact decoded key, skipping complete child subtrees. */
const cfgjson_value_t * cfgjson_get (const cfgjson_value_t * p_value,
                                     const char *            p_key)
{
    if ((!kind(p_value, '{')) || (NULL == p_key))
    {
        return NULL;
    }
    for (size_t index = first_child(p_value); index < p_value->next;
         index        = g_nodes[index].next)
    {
        if (text_equal(g_nodes[index].key, p_key))
        {
            return &g_nodes[index];
        }
    }
    return NULL;
}

/** @brief Index only immediate array values, never their descendants. */
const cfgjson_value_t * cfgjson_at (const cfgjson_value_t * p_value,
                                    size_t                  index)
{
    if (!kind(p_value, '['))
    {
        return NULL;
    }
    size_t child = first_child(p_value);
    while ((child < p_value->next) && (0U != index))
    {
        child = g_nodes[child].next;
        --index;
    }
    return (child < p_value->next) ? &g_nodes[child] : NULL;
}

/** @brief Count immediate array values under the caller's explicit limit. */
bool cfgjson_array (const cfgjson_value_t * p_value,
                    size_t                  max,
                    size_t *                p_count)
{
    if ((!kind(p_value, '[')) || (NULL == p_count))
    {
        return false;
    }
    size_t count = 0U;
    for (size_t index = first_child(p_value); index < p_value->next;
         index        = g_nodes[index].next)
    {
        ++count;
        if (count > max)
        {
            return false;
        }
    }
    *p_count = count;
    return true;
}

/** @brief Reject unknown members and enforce the existing required-field mask.
 */
bool cfgjson_object (const cfgjson_value_t * p_value,
                     const char * const *    pp_keys,
                     size_t                  count,
                     uint32_t                required)
{
    if ((!kind(p_value, '{')) || (NULL == pp_keys) || (count > 16U))
    {
        return false;
    }
    uint32_t seen = 0U;
    for (size_t child = first_child(p_value); child < p_value->next;
         child        = g_nodes[child].next)
    {
        size_t index = 0U;
        while ((index < count) &&
               (!text_equal(g_nodes[child].key, pp_keys[index])))
        {
            ++index;
        }
        if (index == count)
        {
            return false;
        }
        seen |= UINT32_C(1) << index;
    }
    return required == (seen & required);
}

/** @brief Compare a decoded string without storing a second copy. */
bool cfgjson_is (const cfgjson_value_t * p_value, const char * p_text)
{
    return (kind(p_value, '"')) && (NULL != p_text) &&
           (text_equal(p_value->start, p_text));
}

/** @brief Copy decoded ASCII only when the caller's capacity includes NUL. */
bool cfgjson_text (const cfgjson_value_t * p_value,
                   char *                  p_text,
                   size_t                  capacity)
{
    if ((!kind(p_value, '"')) || (NULL == p_text) || (0U == capacity))
    {
        return false;
    }
    size_t offset = p_value->start + 1U;
    size_t count  = 0U;
    while (offset < p_value->end)
    {
        const uint8_t character = decoded(&offset);
        if (count == capacity)
        {
            return false;
        }
        /* Lexical validation restricts decoded bytes to ASCII 0..127. */
        p_text[count++] = (char)character;
        if (0U == character)
        {
            return true;
        }
    }
    return false;
}

/** @brief Preserve output on failure while validating a complete identifier. */
bool cfgjson_name (const cfgjson_value_t * p_value, char p_name[32])
{
    char text[32] = {0};
    if ((NULL == p_name) || (!cfgjson_text(p_value, text, sizeof(text))))
    {
        return false;
    }
    const size_t size = strlen(text);
    if (0U == size)
    {
        return false;
    }
    for (size_t index = 0U; index < size; ++index)
    {
        const char character = text[index];
        const bool b_letter  = ((character >= 'A') && (character <= 'Z')) ||
                              ((character >= 'a') && (character <= 'z'));
        const bool b_extra = ((character >= '0') && (character <= '9')) ||
                             ('_' == character) || ('.' == character) ||
                             ('-' == character);
        if ((!b_letter) && ((0U == index) || (!b_extra)))
        {
            return false;
        }
    }
    memcpy(p_name, text, size + 1U);
    return true;
}

/** @brief Preserve exact uint32 values without a floating-point conversion. */
bool cfgjson_uint (const cfgjson_value_t * p_value,
                   uint32_t                min,
                   uint32_t                max,
                   uint32_t *              p_result)
{
    int64_t number = 0;
    if ((NULL == p_result) || (!integer(p_value, &number)) || (number < min) ||
        (number > max))
    {
        return false;
    }
    /* The supplied unsigned range bounds this exact integer to uint32. */
    *p_result = (uint32_t)number;
    return true;
}

/** @brief Preserve the full signed int32 range, including INT32_MIN. */
bool cfgjson_int (const cfgjson_value_t * p_value,
                  int32_t                 min,
                  int32_t                 max,
                  int32_t *               p_result)
{
    int64_t number = 0;
    if ((NULL == p_result) || (!integer(p_value, &number)) || (number < min) ||
        (number > max))
    {
        return false;
    }
    /* The supplied signed range bounds this exact integer to int32. */
    *p_result = (int32_t)number;
    return true;
}

/** @brief Require true/false literals; numbers never stand in for Booleans. */
bool cfgjson_bool (const cfgjson_value_t * p_value, bool * pb_result)
{
    if ((NULL == pb_result) || ((!kind(p_value, 't')) && (!kind(p_value, 'f'))))
    {
        return false;
    }
    *pb_result = kind(p_value, 't');
    return true;
}

/** @brief Advance only across the four JSON whitespace characters. */
static void whitespace (size_t * p_offset)
{
    while ((*p_offset < g_size) &&
           (NULL != strchr(" \t\r\n", gp_data[*p_offset])))
    {
        ++(*p_offset);
    }
}

/** @brief Keep the former parse-work ceiling without allocation hooks. */
static bool step (void)
{
    if (UTHOME_JSON_STEPS == g_steps)
    {
        return false;
    }
    ++g_steps;
    return true;
}

/**
 * @brief Decode one byte from a lexically validated quoted ASCII string.
 *
 * The cursor starts after its opening quote and must not pass its closing
 * quote. Preflight proves every escape and four-digit Unicode sequence fits
 * the input, contains no NUL and represents at most 127. Zero marks the closing
 * quote, not an escaped quote. The original input remains immutable until
 * close.
 */
static uint8_t decoded (size_t * p_offset)
{
    uint8_t character = gp_data[(*p_offset)++];
    if ('"' == character)
    {
        return 0U;
    }
    if ('\\' != character)
    {
        return character;
    }
    character = gp_data[(*p_offset)++];
    if ('u' == character)
    {
        uint32_t code = 0U;
        for (size_t digit = 0U; digit < 4U; ++digit)
        {
            const uint32_t part = gp_data[(*p_offset)++];
            const uint32_t value =
                (part <= '9')
                    ? (part - '0')
                    : ((part >= 'a') ? (part - 'a' + 10U) : (part - 'A' + 10U));
            code = (code * 16U) + value;
        }
        /* Preflight restricts the complete Unicode escape to ASCII 1..127. */
        return (uint8_t)code;
    }
    static const char    escapes[] = "\"\\/bfnrt";
    static const uint8_t values[]  = {
        '"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
    for (size_t index = 0U; index < sizeof(values); ++index)
    {
        if (character == escapes[index])
        {
            return values[index];
        }
    }
    /* Preflight excludes every other escape. */
    return 0U;
}

/** @brief Compare decoded ASCII against a trusted NUL-terminated constant. */
static bool text_equal (size_t start, const char * p_text)
{
    size_t offset = start + 1U;
    size_t index  = 0U;
    while (offset < g_size)
    {
        const uint8_t character = decoded(&offset);
        if (character != p_text[index])
        {
            return false;
        }
        if (0U == character)
        {
            return true;
        }
        ++index;
    }
    return false;
}

/** @brief Compare decoded keys, including differently escaped spellings. */
static bool keys_equal (size_t left, size_t right)
{
    ++left;
    ++right;
    while ((left < g_size) && (right < g_size))
    {
        const uint8_t character = decoded(&left);
        if (character != decoded(&right))
        {
            return false;
        }
        if (0U == character)
        {
            return true;
        }
    }
    return false;
}

/** @brief View handles must come from this module and remain within a live
 * tree. */
static bool kind (const cfgjson_value_t * p_value, uint8_t expected)
{
    return (NULL != gp_data) && (NULL != p_value) &&
           (expected == gp_data[p_value->start]);
}

/** @brief Preorder puts the first child immediately after its parent view. */
static size_t first_child (const cfgjson_value_t * p_value)
{
    /* Live views refer to this array; the difference lies in 0..511. */
    return (size_t)(p_value - g_nodes) + 1U;
}

/** @brief Build one bounded preorder view; publish its subtree end on success.
 */
static bool value (size_t * p_offset, size_t depth, uint16_t key)
{
    whitespace(p_offset);
    if ((*p_offset == g_size) || (UTHOME_JSON_NODES == g_count) || (!step()))
    {
        return false;
    }
    const size_t index = g_count++;
    g_peak             = g_count * sizeof(g_nodes[0]);
    /* Input offsets are at most 8192; node indices are at most 512. */
    g_nodes[index] =
        (cfgjson_value_t){.start = (uint16_t)*p_offset, .key = key};
    const uint8_t character   = gp_data[*p_offset];
    const bool    b_container = ('{' == character) || ('[' == character);
    if (b_container ? ((depth >= 8U) || (!container(index, p_offset, depth)))
                    : (!scalar(p_offset)))
    {
        return false;
    }
    /* Both bounds were enforced before advancing the cursor or node count. */
    g_nodes[index].end  = (uint16_t)*p_offset;
    g_nodes[index].next = (uint16_t)g_count;
    return true;
}

/** @brief Enforce separators, object keys and duplicate-key rejection. */
static bool container (size_t index, size_t * p_offset, size_t depth)
{
    const bool    b_object = '{' == gp_data[(*p_offset)++];
    const uint8_t closing  = b_object ? '}' : ']';
    whitespace(p_offset);
    if ((*p_offset < g_size) && (closing == gp_data[*p_offset]))
    {
        ++(*p_offset);
        return true;
    }
    while (*p_offset < g_size)
    {
        uint16_t key = UINT16_MAX;
        if (b_object)
        {
            if (('"' != gp_data[*p_offset]) || (!step()))
            {
                return false;
            }
            /* Preflight limits the complete document to 8192 bytes. */
            key = (uint16_t)(*p_offset);
            ++(*p_offset);
            while (0U != decoded(p_offset))
            {
                /* Consume a lexically validated key through its closing quote.
                 */
            }
            whitespace(p_offset);
            if ((*p_offset == g_size) || (':' != gp_data[*p_offset]))
            {
                return false;
            }
            ++(*p_offset);
            for (size_t child = index + 1U; child < g_count;
                 child        = g_nodes[child].next)
            {
                if (keys_equal(g_nodes[child].key, key))
                {
                    return false;
                }
            }
        }
        if (!value(p_offset, depth + 1U, key))
        {
            return false;
        }
        whitespace(p_offset);
        if (*p_offset == g_size)
        {
            return false;
        }
        const uint8_t separator = gp_data[(*p_offset)++];
        if (closing == separator)
        {
            return true;
        }
        if (',' != separator)
        {
            return false;
        }
        whitespace(p_offset);
        /* A closing delimiter after a comma must be rejected as a missing
         * value. */
    }
    return false;
}

/** @brief Recognize exactly one validated string, integer or JSON literal. */
static bool scalar (size_t * p_offset)
{
    const uint8_t character = gp_data[*p_offset];
    if ('"' == character)
    {
        if (!step())
        {
            return false;
        }
        ++(*p_offset);
        while (0U != decoded(p_offset))
        {
            /* Preflight proves a closing quote and the bounded escape syntax.
             */
        }
        return true;
    }
    if (('-' == character) || ((character >= '0') && (character <= '9')))
    {
        if (!step())
        {
            return false;
        }
        do
        {
            ++(*p_offset);
        } while ((*p_offset < g_size) && (gp_data[*p_offset] >= '0') &&
                 (gp_data[*p_offset] <= '9'));
        return true;
    }
    const char * p_literal = NULL;
    if ('t' == character)
    {
        p_literal = "true";
    }
    else if ('f' == character)
    {
        p_literal = "false";
    }
    else if ('n' == character)
    {
        p_literal = "null";
    }
    else
    {
        return false;
    }
    const size_t size = strlen(p_literal);
    if ((size > (g_size - *p_offset)) ||
        (0 != memcmp(&gp_data[*p_offset], p_literal, size)))
    {
        return false;
    }
    *p_offset += size;
    return true;
}

/** @brief Preflight bounds magnitude to uint32 and negative values to int32. */
static bool integer (const cfgjson_value_t * p_value, int64_t * p_result)
{
    if ((NULL == gp_data) || (NULL == p_value))
    {
        return false;
    }
    size_t        offset     = p_value->start;
    const uint8_t character  = gp_data[offset];
    const bool    b_negative = '-' == character;
    if ((!b_negative) && ((character < '0') || (character > '9')))
    {
        return false;
    }
    if (b_negative)
    {
        ++offset;
    }
    int64_t number = 0;
    while (offset < p_value->end)
    {
        /* Every digit is validated and magnitude <= UINT32_MAX fits int64. */
        number = (number * 10) + (gp_data[offset++] - '0');
    }
    *p_result = b_negative ? -number : number;
    return true;
}

/* End of file. */

