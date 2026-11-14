/**
 * @file jsonscan.h
 * @brief Bound the node JSON lexical subset before constructing compact views.
 */
#ifndef UTHOME_JSONSCAN_H
#define UTHOME_JSONSCAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Node fields contain ASCII names/constants and canonical uint32 or int32
 * integers. Escaped ASCII is supported; NUL, non-ASCII, fractions, exponents
 * and negative zero are rejected. cfgjson validates the complete grammar.
 * The input is exactly size bytes; no terminator or readable extra byte needed.
 */
bool jsonscan_check (const uint8_t * p_data, size_t size);
#endif
/* End of file. */

