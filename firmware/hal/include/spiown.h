/**
 * @file spiown.h
 * @brief Keep one permanent SPI bus lock while bus sessions open and close.
 */
#ifndef UTHOME_SPIOWN_H
#define UTHOME_SPIOWN_H

#include <stdbool.h>
#include <stdint.h>
#include "spibus.h"
#include "buslife.h"

/* Shared lifecycle storage preserves the permanent state footprint. */
typedef buslife_status_t spiown_status_t;
typedef buslife_phase_t  spiown_phase_t;
typedef buslife_create_t spiown_create_t;
typedef buslife_owner_t  spiown_owner_t;
typedef buslife_lock_t   spiown_lock_t;
typedef buslife_unlock_t spiown_unlock_t;
typedef buslife_start_t  spiown_start_t;
typedef buslife_stop_t   spiown_stop_t;
typedef buslife_ops_t    spiown_ops_t;
typedef buslife_t        spiown_t;

#define SPIOWN_UNINIT   BUSLIFE_UNINIT
#define SPIOWN_READY    BUSLIFE_READY
#define SPIOWN_FAILED   BUSLIFE_FAILED
#define SPIOWN_CLOSED   BUSLIFE_CLOSED
#define SPIOWN_OPEN     BUSLIFE_OPEN
#define SPIOWN_RETAINED BUSLIFE_RETAINED

/* p_start receives the settings supplied through the typed wrapper below.
 * Operations and context remain alive for the program lifetime.
 */
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

