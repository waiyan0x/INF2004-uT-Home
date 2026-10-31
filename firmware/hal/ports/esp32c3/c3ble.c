/**
 * @file c3ble.c
 * @brief Diagnostic-only radio progress with visible unadapted allocations.
 */
#include "c3ble.h"

#if defined(UTHOME_BLE_PROBE) && UTHOME_BLE_PROBE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "esp_heap_caps.h"
#include "c3bport.h"
#include "c3gatt.h"
#include "c3task.h"

#if !CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY
#error "This BLE diagnostic requires the SDK flash controller."
#endif

#if UTHOME_BLE_GATT_PROBE
static uint8_t  g_name[]         = "UTHOME-GATT";
static uint32_t g_connections    = 0U;
static uint32_t g_disconnections = 0U;
static uint16_t g_connection     = BLE_HS_CONN_HANDLE_NONE;
static int      g_gap_error      = 0;
static bool     gb_finished      = false;
static int gap_event (struct ble_gap_event * p_event, void * p_context);
static int gatt_window (c3ble_report_t * p_report, c3ble_log_t * p_log);
#else
static uint8_t g_name[] = "UTHOME-DIAG";
#endif
static uint8_t  g_address_type  = 0U;
static int      g_sync_status   = -1;
static uint32_t g_resets        = 0U;
static bool     gb_synchronized = false;
static bool     gb_exited       = false;

static void synchronized (void);
static void reset_notice (int reason);
static void host_task (void * p_context);
static int await_sync (void);
static int advertise (void);
static int checkpoint (c3ble_report_t * p_report, uint32_t index);
static int operation (c3ble_report_t * p_report,
                      c3ble_log_t *    p_log,
                      uint32_t         stage,
                      int              status);

/** @brief Exercise advertising without association, credentials or persistence.
 */
int c3ble_probe (c3ble_report_t * p_report, c3ble_log_t * p_log)
{
    if ((NULL == p_report) || (NULL == p_log))
    {
        return -1;
    }
    *p_report = (c3ble_report_t){0};
    if ((0 != checkpoint(p_report, 0U)) ||
        (0 != operation(p_report, p_log, 1U, nimble_port_init())))
    {
        return -2;
    }
    /* The SDK flash controller does not use the pinned ROM table patch. */
    ble_hs_cfg.sync_cb  = synchronized;
    ble_hs_cfg.reset_cb = reset_notice;
#if UTHOME_BLE_GATT_PROBE
    if (0 != operation(p_report, p_log, 7U, c3gatt_init()))
    {
        return -2;
    }
#endif
    nimble_port_freertos_init(host_task);
#if UTHOME_BLE_GATT_PROBE
    return gatt_window(p_report, p_log);
#else
    if ((0 != operation(p_report, p_log, 2U, await_sync())) ||
        (0 != operation(p_report, p_log, 3U, advertise())) ||
        (0 != c3bport_delay(5000)) || (0 != checkpoint(p_report, 1U)) ||
        (0 != c3task_seal()) || (0 != c3bport_seal()))
    {
        return -3;
    }
    p_report->b_advertised = true;
    if ((0 != operation(p_report, p_log, 4U, ble_gap_adv_stop())) ||
        (0 != operation(p_report, p_log, 5U, advertise())) ||
        (0 != c3bport_delay(5000)) || (0 != checkpoint(p_report, 2U)))
    {
        return -4;
    }
    if ((0 != operation(p_report, p_log, 6U, ble_gap_adv_stop())) ||
        (0 != checkpoint(p_report, 3U)))
    {
        return -5;
    }
    return ((0U == p_report->resets) && (!p_report->b_host_exited)) ? 0 : -6;
#endif
}

/** @brief Publish synchronization state from the SDK host task under exclusion.
 */
static void synchronized (void)
{
    uint8_t address_type = 0U;
    int     status       = ble_hs_util_ensure_addr(0);
    if (0 == status)
    {
        status = ble_hs_id_infer_auto(0, &address_type);
    }
    const uint32_t saved = c3bport_enter();
    g_address_type       = address_type;
    g_sync_status        = status;
    gb_synchronized      = true;
    c3bport_leave(saved);
}

/** @brief Record resets; do not silently restart or hide controller failures.
 */
static void reset_notice (int reason)
{
    const uint32_t saved = c3bport_enter();
    if (UINT32_MAX > g_resets)
    {
        ++g_resets;
    }
    g_sync_status   = reason;
    gb_synchronized = false;
    c3bport_leave(saved);
}

/** @brief The unadapted SDK creates this diagnostic host task and its stack. */
static void host_task (void * p_context)
{
    /* The SDK supplies no application context for this diagnostic host. */
    (void)p_context;
    nimble_port_run();
    const uint32_t saved = c3bport_enter();
    gb_exited            = true;
    c3bport_leave(saved);
    /* Retain the native object and its storage until the diagnostic resets. */
    c3bport_exit();
}

/** @brief A failed SDK task creation cannot leave the controller waiting
 * forever. */
static int await_sync (void)
{
    for (uint32_t attempt = 0U; attempt < 1000U; ++attempt)
    {
        const uint32_t saved    = c3bport_enter();
        const bool     b_ready  = gb_synchronized;
        const bool     b_failed = gb_exited || (0U != g_resets);
        const int      status   = g_sync_status;
        c3bport_leave(saved);
        if (b_failed)
        {
            return -1;
        }
        if (b_ready)
        {
            return status;
        }
        if (0 != c3bport_delay(10))
        {
            return -2;
        }
    }
    return -3;
}

/** @brief Advertise the selected diagnostic name without configuration access.
 */
static int advertise (void)
{
    struct ble_hs_adv_fields  fields       = {0};
    struct ble_gap_adv_params parameters   = {0};
    const uint32_t            saved        = c3bport_enter();
    const uint8_t             address_type = g_address_type;
    const bool b_ready = gb_synchronized && (!gb_exited) && (0U == g_resets);
    c3bport_leave(saved);
    if (!b_ready)
    {
        return -1;
    }
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = g_name;
    fields.name_len         = sizeof(g_name) - 1U;
    fields.name_is_complete = 1U;
    parameters.conn_mode    = BLE_GAP_CONN_MODE_NON;
#if UTHOME_BLE_GATT_PROBE
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
#endif
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const int status     = ble_gap_adv_set_fields(&fields);
    return (0 == status) ? ble_gap_adv_start(address_type,
                                             NULL,
                                             BLE_HS_FOREVER,
                                             &parameters,
#if UTHOME_BLE_GATT_PROBE
                                             gap_event,
#else
                                             NULL,
#endif
                                             NULL)
                         : status;
}

/** @brief Retain resource observations without listing nearby devices. */
static int checkpoint (c3ble_report_t * p_report, uint32_t index)
{
    p_report->free_bytes[index] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    p_report->largest_bytes[index] =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t saved    = c3bport_enter();
    p_report->resets        = g_resets;
    p_report->b_host_exited = gb_exited;
    c3bport_leave(saved);
    return c3bport_capture(&p_report->observations[index]);
}

/** @brief The last completed SDK stage remains visible if a later call stalls.
 */
static int operation (c3ble_report_t * p_report,
                      c3ble_log_t *    p_log,
                      uint32_t         stage,
                      int              status)
{
    p_report->stage  = stage;
    p_report->status = status;
    p_log(stage, status, heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return status;
}

#if UTHOME_BLE_GATT_PROBE
/** @brief One bounded, unauthenticated read-only window; no device settings. */
static int gatt_window (c3ble_report_t * p_report, c3ble_log_t * p_log)
{
    if ((0 != operation(p_report, p_log, 2U, await_sync())) ||
        (0 != operation(p_report, p_log, 3U, advertise())) ||
        (0 != checkpoint(p_report, 1U)) || (0 != c3task_seal()) ||
        (0 != c3bport_seal()))
    {
        return -3;
    }
    p_report->b_advertised = true;
    if (0 != c3bport_delay(45000))
    {
        return -4;
    }
    uint32_t saved            = c3bport_enter();
    gb_finished               = true;
    const uint16_t connection = g_connection;
    c3bport_leave(saved);
    int status = ble_gap_adv_stop();
    if (BLE_HS_EALREADY == status)
    {
        status = 0;
    }
    if ((0 == status) && (BLE_HS_CONN_HANDLE_NONE != connection))
    {
        status = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
    if ((0 != operation(p_report, p_log, 8U, status)) ||
        (0 != c3bport_delay(100)) || (0 != checkpoint(p_report, 2U)))
    {
        return -5;
    }
    saved                    = c3bport_enter();
    p_report->connections    = g_connections;
    p_report->disconnections = g_disconnections;
    status                   = g_gap_error;
    c3bport_leave(saved);
    p_report->reads = c3gatt_reads();
    if ((0 != operation(p_report, p_log, 9U, status)) ||
        (p_report->connections < 2U) || (p_report->reads < 4U) ||
        (p_report->resets > 0U) || (p_report->b_host_exited))
    {
        return -6;
    }
    return checkpoint(p_report, 3U);
}

/** @brief Restart advertising after disconnect without changing configuration.
 */
static int gap_event (struct ble_gap_event * p_event, void * p_context)
{
    (void)p_context;
    if (NULL == p_event)
    {
        return 0;
    }
    bool           b_restart = false;
    const uint32_t saved     = c3bport_enter();
    if (BLE_GAP_EVENT_CONNECT == p_event->type)
    {
        if (0 == p_event->connect.status)
        {
            g_connection = p_event->connect.conn_handle;
            if (g_connections < UINT32_MAX)
            {
                ++g_connections;
            }
        }
        else
        {
            b_restart = !gb_finished;
        }
    }
    else if (BLE_GAP_EVENT_DISCONNECT == p_event->type)
    {
        g_connection = BLE_HS_CONN_HANDLE_NONE;
        if (g_disconnections < UINT32_MAX)
        {
            ++g_disconnections;
        }
        b_restart = !gb_finished;
    }
    else
    {
        /* Other SDK notices do not change the bounded diagnostic state. */
    }
    c3bport_leave(saved);
    if (b_restart)
    {
        const int      status = advertise();
        const uint32_t update = c3bport_enter();
        if (0 != status)
        {
            g_gap_error = status;
        }
        c3bport_leave(update);
    }
    return 0;
}
#endif
#endif

/* End of file. */

