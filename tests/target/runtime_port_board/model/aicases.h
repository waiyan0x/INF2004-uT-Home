/**
 * @file aicases.h
 * @brief Frozen Python predictions for an isolated hardware parity probe.
 */
#ifndef UTHOME_AICASES_H
#define UTHOME_AICASES_H
#include <stdint.h>
#define AICASES_MODEL_SHA256                                                   \
    "6f7bc4e094b8bb35141228494ad85aef6034f47f366e8f44faf2ec1e4f03d7a1"
uint32_t aicases_count (void);
/* Borrow a read-only eighteen-input vector; invalid arguments return null. */
uint32_t const * aicases_get (uint32_t index, int32_t * p_expected);
#endif

/* End of file. */

