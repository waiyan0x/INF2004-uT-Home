/**
 * @file c3store.h
 * @brief Map two record slots into the dedicated configuration partition.
 */
#ifndef UTHOME_C3STORE_H
#define UTHOME_C3STORE_H
#include "cfgstore.h"
#define UTHOME_C3STORE_SLOT 12288U
/* One controller task; singleton partition pointer remains SDK-owned. Probe
 * never erases. Missing/wrong partition returns error, with callbacks remaining
 * usable to report storage faults. Exact custom type/subtype 0x40/0, internal
 * flash, offset 0x110000, size 0x6000, 4096-byte erase geometry, unencrypted,
 * writable. No fallback to NVS or raw physical flash writes. */
int c3store_open (void);
const cfgstore_ops_t * c3store_operations (void);
#endif
/* End of file. */

