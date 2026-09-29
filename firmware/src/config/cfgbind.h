/**
 * @file cfgbind.h
 * @brief Connect independently admitted profiles to real compiled adapters.
 */
#ifndef UTHOME_CFGBIND_H
#define UTHOME_CFGBIND_H
#include "cfgplan.h"
#include "cfgctrl.h"
#include "activate.h"
#include "bmpinst.h"
#include "ldinst.h"
#include "hcinst.h"
#include "statusmon.h"
#include "oledinst.h"
#include "pwminst.h"
typedef int cfgbind_i2c_open_t (const i2cbus_config_t * p_config,
                                i2cbus_t **             pp_bus);
typedef int cfgbind_i2c_close_t (i2cbus_t * p_bus);
typedef uartio_t * cfgbind_uart_t (void);
typedef pulseio_t * cfgbind_pulse_t (uint8_t pair);
typedef pwmio_t * cfgbind_pwm_t (uint8_t channel);

typedef int cfgbind_spi_open_t (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus);
typedef int cfgbind_spi_close_t (spibus_t * p_bus);
typedef int cfgbind_line_open_t (uint8_t     slot,
                                 uint8_t     pin,
                                 bool        b_initial,
                                 digiout_t * p_output);
typedef int cfgbind_line_close_t (digiout_t * p_output);

/* Line open publishes a borrowed handle for every retained resource, even on
 * failure. Close clears the handle only on success; both run with workers
 * stopped. */
typedef struct
{
    cfgbind_i2c_open_t *   p_i2c_open;
    cfgbind_i2c_close_t *  p_i2c_close;
    cfgbind_uart_t *       p_uart;
    cfgbind_pulse_t *      p_pulse;
    cfgbind_pwm_t *        p_pwm;
    cfgbind_spi_open_t *   p_spi_open;
    cfgbind_spi_close_t *  p_spi_close;
    cfgbind_line_open_t *  p_line_open;
    cfgbind_line_close_t * p_line_close;
} cfgbind_io_t;

typedef struct
{
    oledinst_t      instance;
    spibus_device_t device;
    uint8_t         sources;
} cfgbind_display_t;

typedef struct
{
    pwminst_t         instance;
    rulecalc_config_t rule;
    rulecalc_state_t  state;
} cfgbind_output_t;

typedef union
{
    bmpinst_t         environment;
    ldinst_t          radar;
    hcinst_t          distance;
    cfgbind_display_t display;
    cfgbind_output_t  output;
} cfgbind_context_t;
typedef struct cfgbind_state cfgbind_state_t;

typedef struct
{
    cfgbind_state_t *      p_owner;
    cfgbind_context_t      context;
    component_t            component;
    rt_port_step_fn_t *    p_step;
    rt_port_cleanup_fn_t * p_cleanup;
    bool                   b_touched;
    bool                   b_display;
    bool                   b_output;
} cfgbind_slot_t;

/* Noncopyable, zero-initialized, application-lifetime storage. One serialized
 * lifecycle owner; this object may not be reinitialized while retained. Scratch
 * and slots must not be altered by the caller. Initialize cJSON and activate
 * once before init; init itself performs no hardware I/O.
 */
struct cfgbind_state
{
    const cfgbind_io_t * p_io;
    i2cbus_t *           p_i2c;
    spibus_t *           p_spi;
    statusmon_t          monitor;
    cfgplan_t            candidate;
    activate_plan_t      plan;
    cfgbind_slot_t       slots[UTHOME_PLAN_DEVICES];
    bool                 b_initialized;
    bool                 b_candidate;
    bool                 b_busy;
};

int cfgbind_init (cfgbind_state_t * p_state, const cfgbind_io_t * p_io);
const cfgctrl_ops_t * cfgbind_operations (void);
uint32_t cfgbind_mask (void);
/* Owner loop: service the active copied settings, including after rollback. */
int cfgbind_service (cfgbind_state_t * p_state, uint64_t now_ms);
/* Read-only capability payload: compiled bindings, never physical discovery.
 * Uses no context; matches the pump observer callback. See configurator README.
 */
int cfgbind_capabilities (void *    p_context,
                          uint8_t * p_data,
                          size_t    capacity,
                          size_t *  p_size);
#endif
/* End of file. */

