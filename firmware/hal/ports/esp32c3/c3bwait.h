/**
 * @file c3bwait.h
 * @brief Native timing and mutex calls shared by the two peripheral buses.
 */
#ifndef UTHOME_C3BWAIT_H
#define UTHOME_C3BWAIT_H

#include <stdint.h>

#define C3BWAIT_ERROR   (-1)
#define C3BWAIT_TIMEOUT (-2)

/* Create returns a positive native handle or zero on failure. Handles remain
 * owned for the boot lifetime; this boundary never deletes or retries creation.
 * Lock accepts a remaining millisecond budget, including zero for polling.
 * Successful lock/unlock/yield returns zero. The bus maps errors to its domain.
 */
int c3bwait_create (void);
int c3bwait_lock (int h_mutex, uint32_t timeout_ms);
int c3bwait_unlock (int h_mutex);
int c3bwait_yield (void);
void c3bwait_spin_us (uint32_t duration_us);
int c3bwait_source_hz (void);

#endif

/* End of file. */

