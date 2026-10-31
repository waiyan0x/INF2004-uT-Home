/**
 * @file aivects.h
 * @brief Frozen Python predictions for an isolated hardware parity probe.
 */
#ifndef UTHOME_AIVECTS_H
#define UTHOME_AIVECTS_H
#include <stdint.h>
#define AIVECTS_MODEL_SHA256                                                   \
    "96f73ceff2e7943c4f1b23f3d333252d13bffef9c6eccb16ee7281c07d13bd58"
uint32_t aivects_count (void);
/* Borrow a read-only eighteen-input vector; invalid arguments return null. */
uint32_t const * aivects_get (uint32_t index, int32_t * p_expected);
#endif

/* End of file. */

