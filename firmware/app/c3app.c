/**
 * @file c3app.c
 * @brief Compose admission, activation, storage and the USB controller loop.
 */
#include "c3app.h"
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
static cfgbind_state_t g_binding   = {0};
static cfgctrl_t       g_control   = {0};
static cfgstore_t      g_store     = {0};
static cfgpump_t       g_pump      = {0};
static cfgdisc_t       g_discovery = {0};
static telemetry_t     g_telemetry = {.p_control = &g_control};

/** @brief Keep every large context off the kernel task stack. */
int c3app_run (const uint8_t * p_build_hash)
{
    /* Binary traffic exclusively owns USB after the boot banner. No regular
     * console prints or monitor reads are permitted after this handover. */
    if ((E_OK != tk_chg_pri(TSK_SELF, 20)) || (0 != cfgjson_init()) ||
        (0 != activate_init()) || (0 != c3pulse_startup()) ||
        (0 != c3pwm_startup()) || (0 != c3uart_init()) || (0 != c3i2c_init()) ||
        (0 != c3spi_init()) ||
        (0 != cfgbind_init(&g_binding, c3bind_operations())))
    {
        return -1;
    }
    /* Missing storage is reported by restore/requests without silencing USB. */
    (void)c3store_open();
    g_store.p_operations = c3store_operations();
    g_store.slot_bytes   = UTHOME_C3STORE_SLOT;
    if ((0 != cfgdisc_init(&g_discovery,
                           &g_control,
                           &g_binding,
                           p_build_hash,
                           discovery_wait)) ||
        (0 != cfgctrl_init(
                  &g_control, &g_store, cfgdisc_operations(), &g_discovery)) ||
        (0 != c3usb_open()) ||
        (0 != cfgpump_init(&g_pump, &g_control, c3usb_operations(), NULL)) ||
        (0 != cfgpump_observe(&g_pump, telemetry_observe, &g_telemetry)) ||
        (0 != cfgpump_capabilities(&g_pump, cfgbind_capabilities, NULL)) ||
        (0 != cfgpump_discovery(&g_pump, cfgdisc_observe, &g_discovery)))
    {
        return -1;
    }
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
        (void)cfgpump_step(&g_pump, now_ms);
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

