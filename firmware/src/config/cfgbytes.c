/**
 * @file cfgbytes.c
 * @brief Use explicit little-endian fields and a table-free CRC.
 */
#include "cfgbytes.h"

/** @brief Update a reflected CRC without a lookup table or signed shifts. */
uint32_t cfgbytes_crc (uint32_t state, const uint8_t * p_data, size_t size)
{
    for (size_t index = 0U; index < size; ++index)
    {
        state ^= p_data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t low = state & 1U;
            state >>= 1U;
            if (0U != low)
            {
                state ^= UINT32_C(0xEDB88320);
            }
        }
    }
    return state;
}

/** @brief Widen bytes before shifting to avoid signed integer promotion. */
uint32_t cfgbytes_get (const uint8_t * p_data)
{
    return (uint32_t)p_data[0] | ((uint32_t)p_data[1] << 8U) |
           ((uint32_t)p_data[2] << 16U) | ((uint32_t)p_data[3] << 24U);
}

/** @brief Mask each byte explicitly before its narrowing conversion. */
void cfgbytes_put (uint8_t * p_data, uint32_t value)
{
    for (unsigned index = 0U; index < 4U; ++index)
    {
        p_data[index] = (uint8_t)((value >> (index * 8U)) & UINT32_C(0xFF));
    }
}

/* End of file. */

