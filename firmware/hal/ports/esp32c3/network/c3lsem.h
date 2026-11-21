/**
 * @file c3lsem.h
 * @brief Bind lwIP's controller semaphore to permanent storage.
 */
#ifndef UTHOME_C3LSEM_H
#define UTHOME_C3LSEM_H

#include "lwip/sys.h"

/** @brief Reserve the controller record before the station starts lwIP. */
int c3lsem_start (void);

/* Task context only. After cleanup all socket operations must be complete.
 * The permanent controller retains its empty semaphore until reboot. */
sys_sem_t * c3lsem_get (void);
sys_sem_t * c3lsem_init (void);
void c3lsem_deinit (void);

/* Source-local names preserve the SDK's other per-thread records. */
sys_sem_t * c3lsem_native_get (void);
sys_sem_t * c3lsem_native_init (void);
void c3lsem_native_deinit (void);

#endif

/* End of file. */

