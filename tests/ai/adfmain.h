/**
 * @file adfmain.h
 * @brief Test the actual target parser and feature boundary together.
 */
#ifndef UTHOME_ADFMAIN_H
#define UTHOME_ADFMAIN_H

#include <stddef.h>
#include <stdint.h>

int adfmain_run (uint8_t const * p_input,
                 size_t          size,
                 size_t          target,
                 uint32_t        goal_ms,
                 uint32_t *      p_features);

#endif

/* End of file. */

