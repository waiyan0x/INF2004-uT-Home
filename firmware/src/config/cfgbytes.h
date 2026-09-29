/**
 * @file cfgbytes.h
 * @brief Encode fixed-width configuration fields independently of C layout.
 */
#ifndef UTHOME_CFGBYTES_H
#define UTHOME_CFGBYTES_H
#include <stddef.h>
#include <stdint.h>
#define UTHOME_CONFIG_BYTES 8192U
/* CRC-32/ISO-HDLC: start UINT32_MAX, update all chunks, then complement.
 * Bounds/pointer validity belong to the caller; helpers allocate no storage. */
uint32_t cfgbytes_crc (uint32_t state, const uint8_t * p_data, size_t size);
uint32_t cfgbytes_get (const uint8_t * p_data);
void cfgbytes_put (uint8_t * p_data, uint32_t value);
#endif
/* End of file. */

