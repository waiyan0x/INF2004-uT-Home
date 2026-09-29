/** @file telemetry.h
 * @brief telemetry service contract. */
#ifndef UTHOME_TELEMETRY_H
#define UTHOME_TELEMETRY_H

#include "shared.h"
#include "activate.h"
#include "cfgctrl.h"
#define UTHOME_TELEMETRY_HEADER 160U
#define UTHOME_TELEMETRY_QUEUE  32U
#define UTHOME_TELEMETRY_SAMPLE 128U
#define UTHOME_TELEMETRY_MAX    1824U

typedef struct
{
    shared_capture_t  capture;
    activate_status_t runtime;
    const cfgctrl_t * p_control;
    int               service_error;
} telemetry_t;

/* Zero-initialized static storage, same owner as configuration. Observe takes
 * a coherent shared snapshot and samples owner metadata nearby; no I/O, heap,
 * queue consumption, retry-cache change or lock held while encoding.
 * Result 0 success, 1 busy, 2 capture error is in payload word 1. A failed
 * capture sends only the versioned header. See TELEMETRY.md for wire layout. */
int telemetry_observe (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_size);

#endif
/* End of file. */

