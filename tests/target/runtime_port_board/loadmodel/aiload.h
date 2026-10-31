/**
 * @file aiload.h
 * @brief Test-only classifier load carried by diagnostic workload replies.
 */
#ifndef UTHOME_AILOAD_H
#define UTHOME_AILOAD_H

#include <stddef.h>
#include <stdint.h>

#define AILOAD_VERSION UINT32_C(0x41494C31)

/**
 * @brief Alternate 75-second OFF/ON/OFF/ON phases from the first request.
 * @note Serial controller ownership only. Calls the real cost observer, then
 * marks its payload diagnostic-only. Never applies a recommendation. Header
 * words 20..23 contain phase (4 = done), total predictions, batch wall us and
 * cumulative mismatches. Ordinary host decoders must reject this version.
 */
int aiload_observe (void *    p_context,
                    uint8_t * p_data,
                    size_t    capacity,
                    size_t *  p_size);

#endif

/* End of file. */

