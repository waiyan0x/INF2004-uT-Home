/**
 * @file c3iface.h
 * @brief Fixed records for the pinned default Wi-Fi station interface.
 */
#ifndef UTHOME_C3IFACE_H
#define UTHOME_C3IFACE_H

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include <stddef.h>

void * c3iface_alloc (size_t count, size_t size);
char * c3iface_dup (char const * p_text);
void c3iface_free (void * p_memory);
#endif

#endif

/* End of file. */

