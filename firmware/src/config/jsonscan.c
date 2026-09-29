/**
 * @file jsonscan.c
 * @brief Reject ambiguous lexical input and bound recursive parser depth.
 */
#include "jsonscan.h"
#include "cfgbytes.h"
#include <string.h>

static bool quoted (const uint8_t * p_data, size_t size, size_t * p_offset);
static int hex_value (uint8_t value);
static bool number (const uint8_t * p_data, size_t size, size_t * p_offset);

/** @brief Accept only the finite, bounded ASCII node profile vocabulary. */
bool jsonscan_check (const uint8_t * p_data, size_t size)
{
    if ((NULL == p_data) || (0U == size) || (size > UTHOME_CONFIG_BYTES))
    {
        return false;
    }
    uint8_t stack[8] = {0};
    size_t  depth    = 0U;
    size_t  offset   = 0U;
    while (offset < size)
    {
        const uint8_t value = p_data[offset];
        if ('"' == value)
        {
            if (!quoted(p_data, size, &offset))
            {
                return false;
            }
        }
        else if (('-' == value) || ((value >= '0') && (value <= '9')))
        {
            if (!number(p_data, size, &offset))
            {
                return false;
            }
        }
        else
        {
            if (('{' == value) || ('[' == value))
            {
                if (depth >= sizeof(stack))
                {
                    return false;
                }
                stack[depth++] = value;
            }
            else if (('}' == value) || (']' == value))
            {
                if ((0U == depth) ||
                    (stack[depth - 1U] != (('}' == value) ? '{' : '[')))
                {
                    return false;
                }
                --depth;
            }
            else if ((NULL == strchr(" \t\r\n,:truefalsn", value)) ||
                     (0U == value))
            {
                return false;
            }
            else
            { /* Punctuation, JSON literals and whitespace go to cJSON. */
            }
            ++offset;
        }
    }
    return 0U == depth;
}

/** @brief Validate strings without letting escaped quotes alter nesting. */
static bool quoted (const uint8_t * p_data, size_t size, size_t * p_offset)
{
    size_t index = *p_offset + 1U;
    while (index < size)
    {
        uint8_t value = p_data[index++];
        if ('"' == value)
        {
            *p_offset = index;
            return true;
        }
        if ((value < 0x20U) || (value > 0x7EU))
        {
            return false;
        }
        if ('\\' == value)
        {
            if (index >= size)
            {
                return false;
            }
            value = p_data[index++];
            if ('u' == value)
            {
                if ((size - index) < 4U)
                {
                    return false;
                }
                uint32_t code = 0U;
                for (size_t digit = 0U; digit < 4U; ++digit)
                {
                    const int part = hex_value(p_data[index++]);
                    if (part < 0)
                    {
                        return false;
                    }
                    /* hex_value is restricted to 0..15. */
                    code = (code * 16U) + (uint32_t)part;
                }
                if ((0U == code) || (code > 0x7FU))
                {
                    return false;
                }
            }
            else if ((0U == value) || (NULL == strchr("\"\\/bfnrt", value)))
            {
                return false;
            }
            else
            { /* The vendor parser decodes the validated escape. */
            }
        }
    }
    return false;
}

/** @brief Decode one hexadecimal digit without locale dependence. */
static int hex_value (uint8_t value)
{
    if ((value >= '0') && (value <= '9'))
    {
        return value - '0';
    }
    if ((value >= 'a') && (value <= 'f'))
    {
        return value - 'a' + 10;
    }
    if ((value >= 'A') && (value <= 'F'))
    {
        return value - 'A' + 10;
    }
    return -1;
}

/** @brief Enforce canonical uint32 or negative int32 tokens; reject negative
 * zero. */
static bool number (const uint8_t * p_data, size_t size, size_t * p_offset)
{
    size_t     index      = *p_offset;
    const bool b_negative = '-' == p_data[index];
    if (b_negative)
    {
        ++index;
    }
    const size_t start = index;
    uint32_t     value = 0U;
    while ((index < size) && (p_data[index] >= '0') && (p_data[index] <= '9'))
    {
        const uint32_t digit = p_data[index] - '0';
        if (value > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }
        value = (value * 10U) + digit;
        ++index;
    }
    if ((index == start) ||
        (b_negative && ((0U == value) || (value > UINT32_C(2147483648)))) ||
        (((index - start) > 1U) && ('0' == p_data[start])))
    {
        return false;
    }
    if ((index < size) && ((0U == p_data[index]) ||
                           (NULL == strchr(" \t\r\n,}]", p_data[index]))))
    {
        return false;
    }
    *p_offset = index;
    return true;
}

/* End of file. */

