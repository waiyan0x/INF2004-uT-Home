/**
 * @file ldparse.h
 * @brief Decode LD2450 reports without a transport or scheduler dependency.
 */
#ifndef UTHOME_LDPARSE_H
#define UTHOME_LDPARSE_H
#include <stddef.h>
#include "uthome/sample.h"
#define UTHOME_LD_FRAME_BYTES 30U

typedef struct
{
    uint8_t  bytes[UTHOME_LD_FRAME_BYTES];
    size_t   used;
    uint32_t frames;
    uint32_t framing_errors;
    uint32_t discarded_bytes;
} ldparse_t;

/* Zero before use. False leaves the output untouched; true replaces it with
 * three preserved slots, status OK, source NO_SOURCE and the supplied time.
 * Counters wrap modulo 2^32. No checksum exists in this wire protocol. */
bool ldparse_feed (ldparse_t *              p_parser,
                   uint8_t                  byte,
                   uint64_t                 now_ms,
                   sample_target_report_t * p_report);
void ldparse_reset (ldparse_t * p_parser);
#endif
/* End of file. */

