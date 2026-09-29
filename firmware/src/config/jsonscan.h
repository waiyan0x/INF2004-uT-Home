/**
 * @file jsonscan.h
 * @brief Bound the node JSON lexical subset before calling the vendor parser.
 */
#ifndef UTHOME_JSONSCAN_H
#define UTHOME_JSONSCAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Node fields contain ASCII names/constants and unsigned decimal integers.
 * Escaped ASCII is supported; NUL, non-ASCII, fractional/exponent/negative
 * numbers are rejected. Container grammar remains cJSON's responsibility.
 * The input is exactly size bytes; no terminator or readable extra byte needed.
 */
bool jsonscan_check (const uint8_t * p_data, size_t size);
#endif
/* End of file. */

