/** @file telemetry.h
 * @brief telemetry service contract. */
#ifndef UTHOME_TELEMETRY_H
#define UTHOME_TELEMETRY_H

#include "shared.h"
#include "activate.h"
#include "cfgctrl.h"
#include "memaudit.h"
#define UTHOME_TELEMETRY_HEADER 352U
#define UTHOME_TELEMETRY_QUEUE  32U
#define UTHOME_TELEMETRY_SAMPLE 128U
#define UTHOME_TELEMETRY_MAX    2016U

typedef int telemetry_audit_t (memaudit_t * p_state);

typedef struct
{
    shared_capture_t    capture;
    activate_status_t   runtime;
    const cfgctrl_t *   p_control;
    int                 service_error;
    memaudit_t          audit;
    telemetry_audit_t * p_audit;
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

