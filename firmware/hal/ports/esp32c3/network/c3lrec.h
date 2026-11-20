/**
 * @file c3lrec.h
 * @brief Fixed typed lwIP socket, TCP connection and segment records.
 */
#ifndef UTHOME_C3LREC_H
#define UTHOME_C3LREC_H

#include "lwip/memp.h"

/* Single-core task context, with cache enabled. Exhaustion returns NULL.
 * Callers must finish SDK cleanup and exclude all users before release.
 * Packet data and mailboxes have separate owners. A released pointer
 * cannot identify a later lease at the same address. */
void * c3lrec_alloc (memp_t kind);
void c3lrec_free (memp_t kind, void * p_memory);

#endif

/* End of file. */

