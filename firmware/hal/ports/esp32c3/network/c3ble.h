/**
 * @file c3ble.h
 * @brief Isolated BLE feasibility and visible allocation observations.
 */
#ifndef UTHOME_C3BLE_H
#define UTHOME_C3BLE_H

#include "memaudit.h"
#include <stdint.h>

typedef void c3ble_log_t (uint32_t stage, int32_t status, uint32_t free_bytes);

typedef struct
{
    memaudit_t observations[4];
    uint32_t   free_bytes[4];
    uint32_t   largest_bytes[4];
    uint32_t   stage;
    int32_t    status;
    uint32_t   resets;
    uint32_t   connections;
    uint32_t   disconnections;
    uint32_t   reads;
    bool       b_advertised;
    bool       b_host_exited;
} c3ble_report_t;

/**
 * @brief Run the selected bounded advertising or read-only GATT diagnostic.
 * @note The default selection is nonconnectable without GATT. Neither mode
 * requests pairing, NVS initialization or flash erase. SDK/controller/NimBLE
 * heap use is explicitly unqualified. The
 * USB core is retained but inactive. The host remains running until reset.
 * The separate GATT build selects a 45-second read-only connection window;
 * it requires two connections and four successful live-uptime reads.
 */
int c3ble_probe (c3ble_report_t * p_report, c3ble_log_t * p_log);

#endif

/* End of file. */

