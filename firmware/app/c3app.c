/**
 * @file c3app.c
 * @brief Compose admission, activation, storage and the USB controller loop.
 */
#include "c3app.h"
#include "c3rule.h"
#include "c3net.h"
#include "c3tx.h"
#include "c3audit.h"
#include "c3clock.h"
#if defined(UTHOME_STATIC_TASKS) && UTHOME_STATIC_TASKS
#include "c3task.h"
#endif
#include <tk/tkernel.h>
#include "cfgjson.h"
#include "c3bind.h"
#include "c3i2c.h"
#include "c3pulse.h"
#include "c3pwm.h"
#include "c3spi.h"
#include "c3store.h"
#include "c3usb.h"
#include "c3uart.h"
#include "activate.h"
#include "telemetry.h"
#include "cfgdisc.h"
static int discovery_wait (void);
#if UTHOME_NETWORK_WIFI
static cfgmux_t g_mux = {0};
#endif
static cfgbind_state_t g_binding   = {0};
static cfgctrl_t       g_control   = {0};
static cfgstore_t      g_store     = {0};
static cfgpump_t       g_pump      = {0};
static cfgdisc_t       g_discovery = {0};
static costview_t      g_cost      = {.p_control    = &g_control,
                                      .p_clock      = c3clock_now,
                                      .p_controller = c3audit_stack_unused};
static telemetry_t     g_telemetry = {.p_control = &g_control,
                                      .p_audit   = c3audit_capture};

/** @brief Keep every large context off the kernel task stack. */
int c3app_run (const uint8_t *       p_build_hash,
               costview_stack_fn_t * p_idle,
               costview_cpu_fn_t *   p_cpu)
{
#if UTHOME_RULE_PROBE
    return c3rule_run();
#endif
    g_cost.p_idle = p_idle;
    g_cost.p_cpu  = p_cpu;
    /* Binary traffic exclusively owns USB after the boot banner. No regular
     * console prints or monitor reads are permitted after this handover. */
    if ((E_OK != tk_chg_pri(TSK_SELF, 20)) || (0 != cfgjson_init()) ||
        (0 != rt_port_clock_set(c3clock_now)) || (0 != activate_init()) ||
        (0 != c3pulse_startup()) || (0 != c3pwm_startup()) ||
        (0 != c3uart_init()) || (0 != c3i2c_init()) || (0 != c3spi_init()) ||
        (0 != cfgbind_init(&g_binding, c3bind_operations())))
    {
        return -1;
    }
    /* Missing storage is reported by restore/requests without silencing USB. */
    (void)c3store_open();
    g_store.p_operations            = c3store_operations();
    g_store.slot_bytes              = UTHOME_C3STORE_SLOT;
    g_store.p_workspace             = g_control.cached;
    g_store.workspace_size          = sizeof(g_control.cached);
    const cfgdisc_io_t discovery_io = {.p_open  = c3i2c_open,
                                       .p_close = c3i2c_close,
                                       .p_uart  = c3uart_port(),
                                       .p_wait  = discovery_wait};
    if ((0 != cfgdisc_init(&g_discovery,
                           &g_control,
                           &g_binding,
                           p_build_hash,
                           &discovery_io)) ||
        (0 != cfgctrl_init(
                  &g_control, &g_store, cfgdisc_operations(), &g_discovery)) ||
        (0 != c3usb_open()) ||
        (0 != cfgpump_init(&g_pump, &g_control, c3usb_operations(), NULL)) ||
        (0 != cfgpump_observe(&g_pump, telemetry_observe, &g_telemetry)) ||
        (0 != cfgpump_capabilities(&g_pump, cfgbind_capabilities, NULL)) ||
        (0 != cfgpump_discovery(&g_pump, cfgdisc_observe, &g_discovery)) ||
        (0 != cfgpump_workcost(&g_pump, costview_observe, &g_cost)))
    {
        return -1;
    }
#if UTHOME_NETWORK_WIFI || UTHOME_NETWORK_BLE
    /* Dynamic transport failures leave local restore/USB available. A static
     * build separately requires the complete reviewed task set before seal. */
#if UTHOME_NETWORK_WIFI
    cfgmux_t * p_upload =
        (0 == cfgmux_init(&g_mux, &g_control, &g_pump)) ? &g_mux : NULL;
    (void)c3tx_start(&g_binding, p_upload);
    /* Failed optional arbitration leaves USB/local operation available. */
    (void)c3net_start(&g_control, p_upload);
#else
    (void)c3net_start(&g_control, NULL);
#endif
#endif
#if defined(UTHOME_STATIC_TASKS) && UTHOME_STATIC_TASKS
    if (0 != c3task_seal())
    {
        return -1;
    }
#endif
    if (0 != c3audit_seal())
    {
        return -1;
    }
    /* Restore is normal operation too. Its allocations must remain visible. */
    /* Restore is attempted once; failures remain observable through QUERY. */
    (void)cfgctrl_restore(&g_control);
    for (;;)
    {
        uint64_t now_ms = 0U;
        if (0 != rt_port_now_ms(&now_ms))
        {
            return -1;
        }
        g_telemetry.service_error = cfgbind_service(&g_binding, now_ms);
        /* A malformed/disconnected transport never stops the active plan. */
#if UTHOME_NETWORK_WIFI
        if ((!cfgmux_busy(&g_mux)) && (!c3tx_busy()))
        {
            (void)cfgpump_step(&g_pump, now_ms);
        }
#else
        (void)cfgpump_step(&g_pump, now_ms);
#endif
#if UTHOME_NETWORK_WIFI || UTHOME_NETWORK_BLE
        /* Same owner captures profile metadata after any USB plan change. */
        (void)c3net_step(&g_control, now_ms);
#endif
        if (E_OK != tk_dly_tsk(1))
        {
            return -1;
        }
    }
}

/** @brief Yield the idle controller during a bounded discovery window. */
static int discovery_wait (void)
{
    return (E_OK == tk_dly_tsk(1)) ? 0 : -1;
}

/* End of file. */

