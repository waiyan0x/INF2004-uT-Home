/**
 * @file c3wifi.h
 * @brief Isolated, noncompliant Wi-Fi feasibility and allocation diagnostic.
 */
#ifndef UTHOME_C3WIFI_H
#define UTHOME_C3WIFI_H

#include "memaudit.h"
#include <stdint.h>

typedef void c3wifi_log_t (uint32_t stage, int32_t status, uint32_t free_bytes);

typedef struct
{
    memaudit_t observations[4];
    uint32_t   free_bytes[4];
    uint32_t   largest_bytes[4];
    uint16_t   access_points[2];
    uint32_t   stage;
    int32_t    status;
} c3wifi_report_t;

/**
 * @brief Scan, stop, restart and scan again; never join or create an AP.
 * @note Diagnostic image only. SDK/shim heap use remains visible and
 * unqualified. No NVS initialization/erase, credentials, IP services or SSID
 * output occurs. Caller provides fixed report storage and remains the
 * controller task.
 */
int c3wifi_probe (c3wifi_report_t * p_report, c3wifi_log_t * p_log);

#endif

/* End of file. */

