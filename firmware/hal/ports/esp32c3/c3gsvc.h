/**
 * @file c3gsvc.h
 * @brief Onboard read-only NimBLE identity/status and sensor notifications.
 */
#ifndef UTHOME_C3GSVC_H
#define UTHOME_C3GSVC_H
#include "netread.h"
/* One startup attempt per boot. Existing SDK host task/allocations are used
 * without changing compatibility code; static/coexistence qualification open.
 */
int c3gsvc_start (const netread_info_t * p_info);
/* App owner captures cache; callbacks only exchange short protected state.
 * Rotates sources every second; one fragment attempt per owner step.
 */
int c3gsvc_step (uint64_t               now_ms,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context);
#endif

/* End of file. */

