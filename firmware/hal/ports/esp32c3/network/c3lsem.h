/**
 * @file c3lsem.h
 * @brief Bind lwIP's controller semaphore to permanent storage.
 */
#ifndef UTHOME_C3LSEM_H
#define UTHOME_C3LSEM_H

#include "lwip/sys.h"
#include <pthread.h>

typedef void c3lsem_destructor_t (void * p_value);

/** @brief Bind the SDK socket destructor to a permanent registry slot. */
int c3lsem_key (pthread_key_t * p_key, c3lsem_destructor_t * p_destructor);

/** @brief Return a native pthread registry slot or NULL when both are live. */
void * c3lsem_reserve (void);

/** @brief Release a fixed record after native key deletion unlinks it. */
void c3lsem_unlinked (const void * p_entry);

/** @brief Reserve a record until native destroy or initialization rollback. */
void * c3lsem_mutex_reserve (void);

/** @brief Return the record after the SDK releases its semaphore lease. */
void c3lsem_mutex_release (const void * p_record);

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

