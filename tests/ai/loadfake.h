/**
 * @file loadfake.h
 * @brief Deterministic host clock and workload observer for the load probe.
 */
#ifndef UTHOME_LOADFAKE_H
#define UTHOME_LOADFAKE_H

#include <stddef.h>
#include <stdint.h>

void loadfake_set (uint64_t now_us);
int loadfake_now (uint64_t * p_now_us);
int loadfake_cost (void *    p_context,
                   uint8_t * p_data,
                   size_t    capacity,
                   size_t *  p_size);

#endif

/* End of file. */

