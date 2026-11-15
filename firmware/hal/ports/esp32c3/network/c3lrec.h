/**
 * @file c3lrec.h
 * @brief Fixed typed lwIP connection and datagram-envelope records.
 */
#ifndef UTHOME_C3LREC_H
#define UTHOME_C3LREC_H

#include "lwip/memp.h"

/* Task context with cache enabled on the single-core C3. Only NETCONN and
 * NETBUF records are supported; exhaustion returns NULL without heap fallback.
 * SDK callers must finish cleanup and exclude all record users before release.
 * Pointees (PCBs, packet buffers, mailboxes) remain owned by lwIP. A released
 * pointer is invalid and cannot identify a later lease at the same address. */
void * c3lrec_alloc (memp_t kind);
void c3lrec_free (memp_t kind, void * p_memory);

#endif

/* End of file. */

