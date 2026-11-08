/**
 * @file c3wifi.c
 * @brief Measure bounded radio startup/restart without network association.
 */
#include "c3wifi.h"

#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "c3audit.h"
#include "c3task.h"

static int checkpoint (c3wifi_report_t * p_report, uint32_t index);
static int operation (c3wifi_report_t * p_report,
                      c3wifi_log_t *    p_log,
                      uint32_t          stage,
                      esp_err_t         status);
static esp_err_t scan (uint16_t * p_count);

/** @brief Keep all SDK allocations observable; this is not static acceptance.
 */
int c3wifi_probe (c3wifi_report_t * p_report, c3wifi_log_t * p_log)
{
    if ((NULL == p_report) || (NULL == p_log))
    {
        return -1;
    }
    *p_report = (c3wifi_report_t){0};
    if (0 != checkpoint(p_report, 0U))
    {
        return -1;
    }
    esp_err_t status = esp_timer_init();
    if (ESP_ERR_INVALID_STATE == status)
    {
        /* This SDK result means that the timer service was already initialized.
         */
        status = ESP_OK;
    }
    if ((0 != operation(p_report, p_log, 1U, status)) ||
        (0 != operation(p_report, p_log, 2U, esp_event_loop_create_default())))
    {
        return -2;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable         = false;
    if ((0 != operation(p_report, p_log, 3U, esp_wifi_init(&config))) ||
        (0 !=
         operation(
             p_report, p_log, 4U, esp_wifi_set_storage(WIFI_STORAGE_RAM))) ||
        (0 !=
         operation(p_report, p_log, 5U, esp_wifi_set_mode(WIFI_MODE_STA))) ||
        (0 != operation(p_report, p_log, 6U, esp_wifi_start())) ||
        (0 !=
         operation(p_report, p_log, 7U, scan(&p_report->access_points[0]))) ||
        (0 != checkpoint(p_report, 1U)) || (0 != c3task_seal()) ||
        (0 != c3audit_seal()))
    {
        return -3;
    }
    if ((0 != operation(p_report, p_log, 8U, esp_wifi_stop())) ||
        (0 != operation(p_report, p_log, 9U, esp_wifi_start())) ||
        (0 !=
         operation(p_report, p_log, 10U, scan(&p_report->access_points[1]))) ||
        (0 != checkpoint(p_report, 2U)))
    {
        return -4;
    }
    if ((0 != operation(p_report, p_log, 11U, esp_wifi_stop())) ||
        (0 != checkpoint(p_report, 3U)))
    {
        return -5;
    }
    return 0;
}

/** @brief Capture actual SDK heap availability beside fixed allocation counts.
 */
static int checkpoint (c3wifi_report_t * p_report, uint32_t index)
{
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if ((4U <= index) || (UINT32_MAX < free_bytes) || (UINT32_MAX < largest))
    {
        return -1;
    }
    p_report->free_bytes[index]    = (uint32_t)free_bytes;
    p_report->largest_bytes[index] = (uint32_t)largest;
    return c3audit_capture(&p_report->observations[index]);
}

/** @brief Identify the failing SDK stage before a diagnostic can stall. */
static int operation (c3wifi_report_t * p_report,
                      c3wifi_log_t *    p_log,
                      uint32_t          stage,
                      esp_err_t         status)
{
    p_report->stage         = stage;
    p_report->status        = status;
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    /* ESP32-C3 internal RAM size is representable in the diagnostic wire width.
     */
    p_log(stage, status, (uint32_t)free_bytes);
    return (ESP_OK == status) ? 0 : -1;
}

/** @brief Retain counts only and release the driver's temporary scan list. */
static esp_err_t scan (uint16_t * p_count)
{
    wifi_scan_config_t config = {0};
    config.scan_type          = WIFI_SCAN_TYPE_PASSIVE;
    config.scan_time.passive  = 100U;
    esp_err_t status          = esp_wifi_scan_start(&config, true);
    if (ESP_OK == status)
    {
        status                  = esp_wifi_scan_get_ap_num(p_count);
        const esp_err_t cleanup = esp_wifi_clear_ap_list();
        if (ESP_OK == status)
        {
            status = cleanup;
        }
    }
    return status;
}

#endif

/* End of file. */

