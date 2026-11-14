/**
 * @file c3lbox.h
 * @brief Fixed lwIP mailbox bindings for the pinned SDK queue ABI.
 */
#ifndef UTHOME_C3LBOX_H
#define UTHOME_C3LBOX_H

#include "lwip/sys.h"

/**
 * @brief Lease a pointer queue from the existing eight shared queue slots.
 *
 * Task/cache-on only, after c3nq_init. The output must not own a live mailbox.
 * Invalid size or exhaustion returns ERR_MEM and a null output, with no
 * fallback.
 */
err_t c3lbox_new (struct sys_mbox_s ** pp_box, int size);

/**
 * @brief Release an empty, unused mailbox and invalidate the caller's handle.
 *
 * The caller must stop producers/consumers before closing, as required by lwIP.
 * A foreign, nonempty or busy lease is retained; no borrowed message is freed.
 */
void c3lbox_free (struct sys_mbox_s ** pp_box);

#endif

/* End of file. */

