/**
 * @file c3store.h
 * @brief Map two record slots into the dedicated configuration partition.
 */
#ifndef UTHOME_C3STORE_H
#define UTHOME_C3STORE_H
#include "cfgstore.h"
#include "cfgpart.h"
#define UTHOME_C3STORE_SLOT 12288U
/* One controller task; partition descriptor and 256-byte scratch are static.
 * Probe never erases. Missing/wrong partition returns error, with callbacks
 * remaining usable to report storage faults. Exact custom type/subtype 0x40/0,
 * internal flash, offset 0x110000, size 0x6000, 4096-byte erase geometry,
 * unencrypted, writable. The complete canonical table must match before
 * enabling storage. No fallback to NVS or raw physical flash writes. */
int c3store_open (void);
const cfgstore_ops_t * c3store_operations (void);
/* Scoped SDK flash-guard adaptation. Before successful layout verification,
 * all writes fail. Afterwards only the configuration partition is writable.
 * This callback performs no logging, allocation or kernel calls. */
bool c3store_region_allowed (size_t address, size_t size);
#endif
/* End of file. */

