/** @file c3gatt.h
 * @brief Read-only diagnostic identity and live uptime over one BLE service.
 */
#ifndef UTHOME_C3GATT_H
#define UTHOME_C3GATT_H

#include <stdint.h>

/* Register before starting the host. No configuration, pairing or persistence
 * interface. SDK/NimBLE memory remains audited and explicitly unqualified. */
int c3gatt_init (void);
uint32_t c3gatt_reads (void);

#endif

/* End of file. */

