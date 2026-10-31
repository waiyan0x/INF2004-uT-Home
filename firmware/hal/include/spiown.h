/**
 * @file spiown.h
 * @brief Keep one permanent SPI bus lock while bus sessions open and close.
 */
#ifndef UTHOME_SPIOWN_H
#define UTHOME_SPIOWN_H

#include <stdbool.h>
#include <stdint.h>
#include "spibus.h"

/* Permanent lifecycle. Only spiown_init leaves UNINIT, exactly once. */
typedef enum
{
    SPIOWN_UNINIT = 0,
    SPIOWN_READY  = 1,
    SPIOWN_FAILED = 2
} spiown_status_t;

/* Session phase. RETAINED means cleanup failed: retry close, never reuse. */
typedef enum
{
    SPIOWN_CLOSED   = 0,
    SPIOWN_OPEN     = 1,
    SPIOWN_RETAINED = 2
} spiown_phase_t;

/* Create the permanent lock; called at most once, during spiown_init. */
typedef int spiown_create_t (void * p_context);
/* Report whether the caller is the serialized lifecycle owner. */
typedef bool spiown_owner_t (void * p_context);
/* Lock within timeout_ms; zero polls. Return 0, SPIBUS_TIMEOUT or a failure.
 */
typedef int spiown_lock_t (void * p_context, uint32_t timeout_ms);
typedef int spiown_unlock_t (void * p_context);
/* Acquire pins and start the controller from the adapter's own validated
 * settings. Failure must leave neither held.
 */
typedef int spiown_start_t (void * p_context, const void * p_settings);
/* Stop the controller and release pins. Failure keeps pin ownership. */
typedef int spiown_stop_t (void * p_context);

typedef struct
{
    spiown_create_t * p_create;
    spiown_owner_t *  p_owner;
    spiown_lock_t *   p_lock;
    spiown_unlock_t * p_unlock;
    spiown_start_t *  p_start;
    spiown_stop_t *   p_stop;
} spiown_ops_t;

/* Zero-initialize permanent storage. Phase changes happen with the lock held.
 * Operations and context remain alive for the life of the program.
 */
typedef struct
{
    const spiown_ops_t * p_operations;
    void *               p_context;
    spiown_status_t      status;
    spiown_phase_t       phase;
} spiown_t;

/* Call once during startup, before any open or transfer. Later calls never
 * create another lock: they return 0 after success or SPIBUS_STATE after a
 * failed initialization, which leaves the bus unavailable until reboot.
 */
int spiown_init (spiown_t *           p_owner,
                 const spiown_ops_t * p_operations,
                 void *               p_context);
/* Lifecycle owner only. Requires READY, CLOSED and no stray attached device;
 * polls the lock. p_settings passes through to p_start unchanged.
 */
int spiown_open (spiown_t *       p_owner,
                 const spibus_t * p_bus,
                 const void *     p_settings);
/* Lifecycle owner only. Rejects attached devices and a transfer holding the
 * lock. A stop failure leaves the session RETAINED for an explicit retry.
 */
int spiown_close (spiown_t * p_owner, const spibus_t * p_bus);
/* Lock for one transfer, then require an OPEN session; pair with spiown_end.
 */
int spiown_begin (spiown_t * p_owner, uint32_t timeout_ms);
int spiown_end (spiown_t * p_owner);
/* Session hint that never consults the owner check. It is exact for the task
 * that changes the phase, including device detach during worker cleanup, and
 * a hint for clock/delay callbacks; spiown_begin is authoritative.
 */
bool spiown_is_open (const spiown_t * p_owner);

#endif

/* End of file. */

