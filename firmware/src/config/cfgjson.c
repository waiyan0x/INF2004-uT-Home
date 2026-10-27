/**
 * @file cfgjson.c
 * @brief Parse bounded JSON with an exclusively owned vendor allocation hook.
 */
#include "cfgjson.h"
#include "jsonscan.h"
#include <string.h>
#include <math.h>

/* Typed nodes preserve alignment and effective type without casting a byte
 * arena to cJSON. Character access to a node's representation is permitted. */
static cfgjson_value_t   g_nodes[UTHOME_JSON_NODES] = {0};
static uint8_t           g_text[UTHOME_JSON_TEXT]   = {0};
static size_t            g_node_count               = 0U;
static size_t            g_text_used                = 0U;
static cfgjson_value_t * gp_tree                    = NULL;
static size_t            g_used                     = 0U;
static size_t            g_peak                     = 0U;
static size_t            g_count                    = 0U;
static bool              gb_initialized             = false;
static bool              gb_parsing                 = false;
static void * allocate (size_t size);
static void release (void * p_memory);
static bool inspect (const cfgjson_value_t * p_value, size_t depth);

/** @brief Install the exclusive bounded hooks once, before task startup. */
int cfgjson_init (void)
{
    if (gb_initialized)
    {
        return -1;
    }
    const cJSON_Hooks hooks = {allocate, release};
    /* Vendor API copies the hook fields and does not retain this object. */
    cJSON_Hooks copy = hooks;
    cJSON_InitHooks(&copy);
    gb_initialized = true;
    return 0;
}

/** @brief Parse exactly the supplied bounded document and reject ambiguity. */
const cfgjson_value_t * cfgjson_open (const uint8_t * p_data, size_t size)
{
    if ((!gb_initialized) || gb_parsing || (NULL != gp_tree) ||
        (!jsonscan_check(p_data, size)))
    {
        return NULL;
    }
    g_peak             = 0U;
    g_count            = 0U;
    gb_parsing         = true;
    const char * p_end = NULL;
    /* char may inspect any byte object; the vendor API reads only size bytes.
     */
    const char * p_text = (const char *)p_data;
    gp_tree             = cJSON_ParseWithLengthOpts(p_text, size, &p_end, 0);
    gb_parsing          = false;
    if (NULL == gp_tree)
    {
        cfgjson_close();
        return NULL;
    }
    while ((p_end < (p_text + size)) && (NULL != strchr(" \t\r\n", *p_end)))
    {
        ++p_end;
    }
    if ((p_end != (p_text + size)) || (0 == cJSON_IsObject(gp_tree)) ||
        (!inspect(gp_tree, 0U)))
    {
        cfgjson_close();
    }
    return gp_tree;
}

/** @brief Release the sole tree; no borrowed node pointer remains valid. */
void cfgjson_close (void)
{
    cJSON_Delete(gp_tree);
    gp_tree      = NULL;
    g_node_count = 0U;
    g_text_used  = 0U;
    g_used       = 0U;
    g_count      = 0U;
}

/** @brief Report occupied pool bytes at the most recent parse's high water. */
size_t cfgjson_peak (void)
{
    return g_peak;
}

/** @brief Perform case-sensitive object lookup within a retained tree. */
const cfgjson_value_t * cfgjson_get (const cfgjson_value_t * p_value,
                                     const char *            p_key)
{
    return cJSON_GetObjectItemCaseSensitive(p_value, p_key);
}

/** @brief Return a bounded child without narrowing an unchecked index. */
const cfgjson_value_t * cfgjson_at (const cfgjson_value_t * p_value,
                                    size_t                  index)
{
    if (index > 1024U)
    {
        return NULL;
    }
    /* The bound is smaller than every supported implementation's INT_MAX. */
    return cJSON_GetArrayItem(p_value, (int)index);
}

/** @brief Validate an array before indexing any caller-owned output. */
bool cfgjson_array (const cfgjson_value_t * p_value,
                    size_t                  max,
                    size_t *                p_count)
{
    if ((NULL == p_count) || (0 == cJSON_IsArray(p_value)))
    {
        return false;
    }
    const int count = cJSON_GetArraySize(p_value);
    if ((count < 0) || ((size_t)count > max))
    {
        return false;
    }
    *p_count = (size_t)count; /* Nonnegative count, bounded above. */
    return true;
}

/** @brief Reject unknown members and enforce a small required-field mask. */
bool cfgjson_object (const cfgjson_value_t * p_value,
                     const char * const *    pp_keys,
                     size_t                  count,
                     uint32_t                required)
{
    if ((0 == cJSON_IsObject(p_value)) || (NULL == pp_keys) || (count > 16U))
    {
        return false;
    }
    uint32_t seen = 0U;
    for (const cfgjson_value_t * p_child = p_value->child; NULL != p_child;
         p_child                         = p_child->next)
    {
        size_t index = 0U;
        while ((index < count) &&
               (0 != strcmp(p_child->string, pp_keys[index])))
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

/** @brief Match a string constant with exact case and content. */
bool cfgjson_is (const cfgjson_value_t * p_value, const char * p_text)
{
    return (0 != cJSON_IsString(p_value)) && (NULL != p_text) &&
           (0 == strcmp(p_value->valuestring, p_text));
}

/** @brief Copy an ASCII identifier only after validating the entire string. */
bool cfgjson_name (const cfgjson_value_t * p_value, char p_name[32])
{
    if ((0 == cJSON_IsString(p_value)) || (NULL == p_name))
    {
        return false;
    }
    const char * p_text = p_value->valuestring;
    const size_t size   = strlen(p_text);
    if ((0U == size) || (size > 31U))
    {
        return false;
    }
    for (size_t index = 0U; index < size; ++index)
    {
        const char value    = p_text[index];
        const bool b_letter = ((value >= 'A') && (value <= 'Z')) ||
                              ((value >= 'a') && (value <= 'z'));
        const bool b_extra = ((value >= '0') && (value <= '9')) ||
                             ('_' == value) || ('.' == value) || ('-' == value);
        if ((!b_letter) && ((0U == index) || (!b_extra)))
        {
            return false;
        }
    }
    memcpy(p_name, p_text, size + 1U);
    return true;
}

/** @brief Convert only finite exact integers in an explicit uint32 range. */
bool cfgjson_uint (const cfgjson_value_t * p_value,
                   uint32_t                min,
                   uint32_t                max,
                   uint32_t *              p_result)
{
    if ((NULL == p_result) || (0 == cJSON_IsNumber(p_value)))
    {
        return false;
    }
    const double value = p_value->valuedouble;
    if ((!isfinite(value)) || (value < min) || (value > max) ||
        (floor(value) != value))
    {
        return false;
    }
    *p_result =
        (uint32_t)value; /* Finite integral value within uint32 bounds. */
    return true;
}

/** @brief Convert finite signed threshold integers only within explicit bounds.
 */
bool cfgjson_int (const cfgjson_value_t * p_value,
                  int32_t                 min,
                  int32_t                 max,
                  int32_t *               p_result)
{
    if ((NULL == p_result) || (0 == cJSON_IsNumber(p_value)))
    {
        return false;
    }
    const double value = p_value->valuedouble;
    if ((!isfinite(value)) || (value < min) || (value > max) ||
        (floor(value) != value))
    {
        return false;
    }
    /* Finite exact integer bounded by the supplied int32 range. */
    *p_result = (int32_t)value;
    return true;
}

/** @brief Require a JSON Boolean rather than treating numbers as truth. */
bool cfgjson_bool (const cfgjson_value_t * p_value, bool * pb_result)
{
    if ((NULL == pb_result) || (0 == cJSON_IsBool(p_value)))
    {
        return false;
    }
    *pb_result = 0 != cJSON_IsTrue(p_value);
    return true;
}

/** @brief Limit every vendor allocation, including temporary number buffers. */
static void * allocate (size_t size)
{
    if ((!gb_parsing) || (0U == size) || (g_count >= UTHOME_JSON_CALLS))
    {
        return NULL;
    }
    void * p_result = NULL;
    if (sizeof(cfgjson_value_t) == size)
    {
        if (g_node_count >= UTHOME_JSON_NODES)
        {
            return NULL;
        }
        p_result = &g_nodes[g_node_count++];
    }
    else
    {
        if (size > (sizeof(g_text) - g_text_used))
        {
            return NULL;
        }
        p_result = &g_text[g_text_used];
        g_text_used += size;
    }
    g_used += size;
    ++g_count;
    if (g_used > g_peak)
    {
        g_peak = g_used;
    }
    return p_result;
}

/** @brief Reclaim the whole parse together, never recycle a borrowed node. */
static void release (void * p_memory)
{
    /* cJSON may release temporary strings or partially constructed trees.
     * Every supplied pointer belongs to one of our fixed pools; close resets
     * them after all vendor traversal has finished, including failed parses. */
    (void)p_memory;
}

/** @brief Reject duplicate decoded keys and nonfinite numbers in every object.
 */
static bool inspect (const cfgjson_value_t * p_value, size_t depth)
{
    if (depth > 8U)
    {
        return false;
    }
    if ((0 != cJSON_IsNumber(p_value)) && (!isfinite(p_value->valuedouble)))
    {
        return false;
    }
    for (const cfgjson_value_t * p_child = p_value->child; NULL != p_child;
         p_child                         = p_child->next)
    {
        if (0 != cJSON_IsObject(p_value))
        {
            for (const cfgjson_value_t * p_old = p_value->child;
                 p_old != p_child;
                 p_old = p_old->next)
            {
                if (0 == strcmp(p_old->string, p_child->string))
                {
                    return false;
                }
            }
        }
        if (!inspect(p_child, depth + 1U))
        {
            return false;
        }
    }
    return true;
}

/* End of file. */

