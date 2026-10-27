/**
 * @file aiprobe.h
 * @brief Isolated classifier parity and wall-time measurements.
 */
#ifndef UTHOME_AIPROBE_H
#define UTHOME_AIPROBE_H

#include "memaudit.h"

typedef struct
{
    uint32_t   vectors;
    uint32_t   mismatches;
    uint32_t   total_us;
    uint32_t   maximum_us;
    uint32_t   batch_maximum_us;
    uint32_t   clock_maximum_us;
    uint32_t   checksum;
    memaudit_t before;
    memaudit_t after;
} aiprobe_report_t;

/* Caller owns the report; this never enables configuration advice or Apply. */
int aiprobe_run (aiprobe_report_t * p_report);

#endif

/* End of file. */

