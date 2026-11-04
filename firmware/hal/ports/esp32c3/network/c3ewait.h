/**
 * @file c3ewait.h
 * @brief Native notifications and context checks for fixed event groups.
 */
#ifndef UTHOME_C3EWAIT_H
#define UTHOME_C3EWAIT_H

#include <stdbool.h>
#include <stdint.h>

#define C3EWAIT_ERROR   (-1)
#define C3EWAIT_TIMEOUT (-2)
#define C3EWAIT_FULL    1

/* No state is retained here. Create returns a positive permanent handle or
 * zero; the caller owns its boot lifetime. Signal returns zero or FULL for an
 * already pending hint. Wait returns zero, TIMEOUT or ERROR and never retries.
 * UINT32_MAX requests an infinite wait; other budgets must fit INT32_MAX ms.
 */
int c3ewait_create (void);
int c3ewait_signal (int h_notification);
int c3ewait_wait (int h_notification, uint32_t milliseconds);
uint32_t c3ewait_enter (void);
void c3ewait_leave (uint32_t saved);
bool c3ewait_available (void);
int c3ewait_context (bool b_blocking);
int64_t c3ewait_now (void);
uint32_t c3ewait_tick_hz (void);

#endif

/* End of file. */

