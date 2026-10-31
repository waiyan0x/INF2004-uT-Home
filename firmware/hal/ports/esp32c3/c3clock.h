/**
 * @file c3clock.h
 * @brief Read the SDK monotonic microsecond clock without allocating.
 */
#ifndef UTHOME_C3CLOCK_H
#define UTHOME_C3CLOCK_H

#include <stdint.h>

/* This clock has the SDK boot epoch, distinct from kernel operating time.
 * Use differences for work duration, never compare it directly to kernel ms.
 */
int c3clock_now (uint64_t * p_now_us);

#endif

/* End of file. */

