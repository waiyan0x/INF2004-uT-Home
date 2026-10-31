/**
 * @file cfgbind.c
 * @brief Bind typed settings after admission, retaining checked resource life.
 */
#include "cfgbind.h"
#include "cfgdev.h"
#include "shared.h"
#include "oledview.h"
#include "uthome/sample.h"
#include <string.h>

typedef struct
{
    ssd1306_config_t panel;
    spibus_config_t  spi;
    uint8_t          data_pin;
    uint8_t          reset_pin;
    uint8_t          sources;
} cfgbind_panel_t;

typedef struct
{
    pwmio_config_t    pwm;
    rulecalc_config_t rule;
} cfgbind_actuator_t;

typedef struct
{
    sampcache_source_t source;
    cfgplan_driver_t   driver;
    uint8_t            pair;
    uint8_t            line_slot;
    uint8_t            enable_pin;

    union
    {
        bmpinst_config_t   environment;
        ldinst_config_t    radar;
        hcinst_config_t    distance;
        cfgbind_panel_t    display;
        cfgbind_actuator_t output;
        seninst_config_t   light;
    } settings;
} cfgbind_setting_t;

typedef struct
{
    i2cbus_config_t i2c;
    bool            b_i2c;
    uint32_t        health_ms;
    uint8_t         health_queues;
    uint8_t         sclk;
    uint8_t         mosi;
    bool            b_spi;
} cfgbind_resource_t;

static int open_display (cfgbind_slot_t *          p_slot,
                         const cfgbind_setting_t * p_config);
static int close_display (cfgbind_slot_t * p_slot);
static int close_light (cfgbind_slot_t * p_slot);
static int display_step (cfgbind_display_t * p_display);
static int output_step (cfgbind_slot_t * p_slot);
static int validate (void * p_context, const uint8_t * p_data, size_t size);
static int apply (void * p_context);
static int recover (void * p_context);
static int open_resources (void * p_context, const void * p_data, size_t size);
static int close_resources (void * p_context);
static int start_device (cfgbind_slot_t *            p_slot,
                         const cfgbind_setting_t *   p_settings,
                         const drvreg_descriptor_t * p_descriptor,
                         void *                      p_driver,
                         const void *                p_config,
                         size_t                      config_size);
static void output_settings (const cfgplan_t *   p_plan,
                             size_t              index,
                             cfgbind_setting_t * p_settings);
static int configure (void * p_context, const void * p_data, size_t size);
static int step (void * p_context);
static int cleanup (void * p_context);
static bool make_plan (cfgbind_state_t * p_state);
static void settings (const cfgplan_t *   p_plan,
                      size_t              index,
                      uint8_t             pair,
                      cfgbind_setting_t * p_settings);
static const cfgctrl_ops_t          g_control      = {validate, apply, recover};
static const component_operations_t g_resource_ops = {.p_configure =
                                                          open_resources};
static const component_operations_t g_device_ops   = {.p_configure = configure};

/** @brief Expose only drivers with complete profile-to-worker binding. */
uint32_t cfgbind_mask (void)
{
    uint32_t supported =
        (UINT32_C(1) << CFGPLAN_ENV) | (UINT32_C(1) << CFGPLAN_RADAR) |
        (UINT32_C(1) << CFGPLAN_DISTANCE) | (UINT32_C(1) << CFGPLAN_OLED) |
        (UINT32_C(1) << CFGPLAN_PWM);
#if defined(UTHOME_SENLUX_APP_PROBE) && UTHOME_SENLUX_APP_PROBE
    supported |= UINT32_C(1) << CFGPLAN_LIGHT;
#endif
    return supported;
}

/** @brief Describe compiled support without advertising hardware acceptance. */
int cfgbind_capabilities (void *    p_context,
                          uint8_t * p_data,
                          size_t    capacity,
                          size_t *  p_size)
{
    /* This observer describes the compiled registry; no live context is needed.
     */
    (void)p_context;
    if ((NULL == p_data) || (NULL == p_size) || (capacity < 352U) ||
        (CFGPLAN_DRIVER_COUNT > 8U))
    {
        return -1;
    }
    memset(p_data, 0, 352U);
    cfgbytes_put(&p_data[0], 1U);
    /* Word 1 is hardware-qualified=false until checkpoint 8. */
    cfgbytes_put(&p_data[8], UTHOME_CONFIG_BYTES);
    cfgbytes_put(&p_data[16], UINT32_C(0x4FB));
    cfgbytes_put(&p_data[20], UTHOME_PLAN_DEVICES);
    cfgbytes_put(&p_data[24], 4U);
    /* Bit zero: rule/PWM routing; bit one: reviewed I2C discovery request. */
    cfgbytes_put(&p_data[28], 7U);
    memcpy(&p_data[32], "esp32-c3-supermini", 18U);
    memcpy(&p_data[64], "2.1.0", 6U);
    uint32_t count = 0U;
    for (uint32_t index = 0U; index < CFGPLAN_DRIVER_COUNT; ++index)
    {
        if (0U != (cfgbind_mask() & (UINT32_C(1) << index)))
        {
            /* The table index is a checked enum and names are at most 31 bytes.
             */
            const char * p_name = cfgdev_name((cfgplan_driver_t)index);
            memcpy(&p_data[96U + (count * 32U)], p_name, strlen(p_name));
            ++count;
        }
    }
    cfgbytes_put(&p_data[12], count);
    *p_size = 96U + (count * 32U);
    return 0;
}

/** @brief Borrow all required backend callbacks without opening hardware. */
int cfgbind_init (cfgbind_state_t * p_state, const cfgbind_io_t * p_io)
{
    if ((NULL == p_state) || (NULL == p_io) || (p_state->b_initialized) ||
        (NULL == p_io->p_i2c_open) || (NULL == p_io->p_i2c_close) ||
        (NULL == p_io->p_uart) || (NULL == p_io->p_pulse) ||
        (NULL == p_io->p_pwm) || (NULL == p_io->p_spi_open) ||
        (NULL == p_io->p_spi_close) || (NULL == p_io->p_line_open) ||
        (NULL == p_io->p_line_close))
    {
        return -1;
    }
    p_state->p_io = p_io;
    for (size_t index = 0U; index < UTHOME_PLAN_DEVICES; ++index)
    {
        p_state->slots[index].p_owner = p_state;
    }
    p_state->b_initialized = true;
    return 0;
}

/** @brief Return the production validation/activation/recovery bridge. */
const cfgctrl_ops_t * cfgbind_operations (void)
{
    return &g_control;
}

/** @brief Service only the active plan, retaining faults for telemetry. */
int cfgbind_service (cfgbind_state_t * p_state, uint64_t now_ms)
{
    if ((NULL == p_state) || (!p_state->b_initialized) || (p_state->b_busy) ||
        (!rt_port_is_owner()))
    {
        return -1;
    }
    activate_status_t status = {0};
    if (0 != activate_get_status(&status))
    {
        return -1;
    }
    return (ACTIVATE_ACTIVE == status.state)
               ? statusmon_step(&p_state->monitor, now_ms)
               : 0;
}

/** @brief Build candidate values only; do not modify any live driver context.
 */
static int validate (void * p_context, const uint8_t * p_data, size_t size)
{
    cfgbind_state_t * p_state = p_context;
    if ((NULL == p_state) || (!p_state->b_initialized) || (p_state->b_busy) ||
        (!rt_port_is_owner()))
    {
        return -1;
    }
    p_state->b_busy      = true;
    p_state->b_candidate = false;
    const int result =
        cfgplan_read(p_data, size, cfgbind_mask(), &p_state->candidate);
    if (0 == result)
    {
        p_state->b_candidate = make_plan(p_state);
    }
    p_state->b_busy = false;
    return p_state->b_candidate ? 0 : -1;
}

/** @brief Explicitly map runtime return codes into controller semantics. */
static int apply (void * p_context)
{
    cfgbind_state_t * p_state = p_context;
    if ((NULL == p_state) || (!p_state->b_initialized) || (p_state->b_busy) ||
        (!p_state->b_candidate) || (!rt_port_is_owner()))
    {
        return 1;
    }
    p_state->b_busy      = true;
    p_state->b_candidate = false;
    const int result     = activate_apply(&p_state->plan);
    p_state->b_busy      = false;
    if (0 == result)
    {
        return 0;
    }
    activate_status_t status = {0};
    if ((UTHOME_ACTIVATE_REJECTED == result) &&
        (0 == activate_get_status(&status)) &&
        (ACTIVATE_NEEDS_RECOVERY != status.state))
    {
        return 1;
    }
    return -1;
}

/** @brief Recover to an empty runtime without silently restarting outputs. */
static int recover (void * p_context)
{
    cfgbind_state_t * p_state = p_context;
    if ((NULL == p_state) || (!p_state->b_initialized) || (p_state->b_busy) ||
        (!rt_port_is_owner()))
    {
        return -1;
    }
    p_state->b_busy      = true;
    p_state->b_candidate = false;
    const int result     = activate_clear();
    p_state->b_busy      = false;
    return result;
}

/** @brief Open a shared controller before any dormant worker is allocated. */
static int open_resources (void * p_context, const void * p_data, size_t size)
{
    cfgbind_state_t * p_state = p_context;
    if ((NULL == p_state) || (NULL == p_data) ||
        (sizeof(cfgbind_resource_t) != size) || (NULL != p_state->p_i2c) ||
        (NULL != p_state->p_spi))
    {
        return -1;
    }
    cfgbind_resource_t resource = {0};
    memcpy(&resource, p_data, sizeof(resource));
    uint64_t now_ms = 0U;
    if ((0 != rt_port_now_ms(&now_ms)) ||
        (0 != statusmon_configure(&p_state->monitor,
                                  resource.health_ms,
                                  resource.health_queues,
                                  now_ms)))
    {
        return -1;
    }
    if (resource.b_i2c)
    {
        const int result =
            p_state->p_io->p_i2c_open(&resource.i2c, &p_state->p_i2c);
        if (0 != result)
        {
            return result;
        }
    }
    return resource.b_spi ? p_state->p_io->p_spi_open(
                                resource.sclk, resource.mosi, &p_state->p_spi)
                          : 0;
}

/** @brief Release shared buses only after every device has relinquished them.
 */
static int close_resources (void * p_context)
{
    cfgbind_state_t * p_state = p_context;
    if (NULL == p_state)
    {
        return -1;
    }
    if (NULL != p_state->p_spi)
    {
        const int result = p_state->p_io->p_spi_close(p_state->p_spi);
        if (0 != result)
        {
            return result;
        }
        p_state->p_spi = NULL;
    }
    if (NULL == p_state->p_i2c)
    {
        return 0;
    }
    const int result = p_state->p_io->p_i2c_close(p_state->p_i2c);
    if (0 == result)
    {
        p_state->p_i2c = NULL;
    }
    return result;
}

/** @brief Select a compiled adapter only after the old slot was cleaned. */
static int configure (void * p_context, const void * p_data, size_t size)
{
    cfgbind_slot_t * p_slot = p_context;
    if ((NULL == p_slot) || (NULL == p_data) || (p_slot->b_touched) ||
        (sizeof(cfgbind_setting_t) != size))
    {
        return -1;
    }
    cfgbind_setting_t config = {0};
    memcpy(&config, p_data, sizeof(config));
    memset(&p_slot->context, 0, sizeof(p_slot->context));
    p_slot->b_display                        = CFGPLAN_OLED == config.driver;
    p_slot->b_output                         = CFGPLAN_PWM == config.driver;
    p_slot->b_light                          = CFGPLAN_LIGHT == config.driver;
    p_slot->p_step                           = NULL;
    const drvreg_descriptor_t * p_descriptor = NULL;
    void *                      p_driver     = NULL;
    const void *                p_config     = NULL;
    size_t                      config_size  = 0U;
    if (CFGPLAN_ENV == config.driver)
    {
        p_slot->context.environment.p_bus = p_slot->p_owner->p_i2c;
        p_descriptor                      = bmpinst_descriptor();
        p_driver                          = &p_slot->context.environment;
        p_config                          = &config.settings.environment;
        config_size                       = sizeof(config.settings.environment);
        p_slot->p_step                    = bmpinst_step;
        p_slot->p_cleanup                 = bmpinst_cleanup;
    }
    else if (CFGPLAN_LIGHT == config.driver)
    {
        p_slot->context.light.instance.p_bus    = p_slot->p_owner->p_i2c;
        p_slot->context.light.instance.p_enable = &p_slot->context.light.enable;
        p_descriptor                            = seninst_descriptor();
        p_driver          = &p_slot->context.light.instance;
        p_config          = &config.settings.light;
        config_size       = sizeof(config.settings.light);
        p_slot->p_step    = seninst_step;
        p_slot->p_cleanup = seninst_cleanup;
    }
    else if (CFGPLAN_RADAR == config.driver)
    {
        p_slot->context.radar.p_port = p_slot->p_owner->p_io->p_uart();
        p_descriptor                 = ldinst_descriptor();
        p_driver                     = &p_slot->context.radar;
        p_config                     = &config.settings.radar;
        config_size                  = sizeof(config.settings.radar);
        p_slot->p_step               = ldinst_step;
        p_slot->p_cleanup            = ldinst_cleanup;
    }
    else if (CFGPLAN_OLED == config.driver)
    {
        p_descriptor      = oledinst_descriptor();
        p_driver          = &p_slot->context.display.instance;
        p_config          = &config.settings.display.panel;
        config_size       = sizeof(config.settings.display.panel);
        p_slot->p_step    = oledinst_step;
        p_slot->p_cleanup = oledinst_cleanup;
    }
    else if (CFGPLAN_PWM == config.driver)
    {
        p_slot->context.output.instance.p_port =
            p_slot->p_owner->p_io->p_pwm(config.pair);
        p_slot->context.output.rule = config.settings.output.rule;
        p_descriptor                = pwminst_descriptor();
        p_driver                    = &p_slot->context.output.instance;
        p_config                    = &config.settings.output.pwm;
        config_size                 = sizeof(config.settings.output.pwm);
        p_slot->p_cleanup           = pwminst_cleanup;
    }
    else if (CFGPLAN_DISTANCE == config.driver)
    {
        p_slot->context.distance.p_port =
            p_slot->p_owner->p_io->p_pulse(config.pair);
        p_descriptor      = hcinst_descriptor();
        p_driver          = &p_slot->context.distance;
        p_config          = &config.settings.distance;
        config_size       = sizeof(config.settings.distance);
        p_slot->p_step    = hcinst_step;
        p_slot->p_cleanup = hcinst_cleanup;
    }
    else
    {
        return -1;
    }
    return start_device(
        p_slot, &config, p_descriptor, p_driver, p_config, config_size);
}

/** @brief Bind the selected adapter and retain partially acquired resources. */
static int start_device (cfgbind_slot_t *            p_slot,
                         const cfgbind_setting_t *   p_settings,
                         const drvreg_descriptor_t * p_descriptor,
                         void *                      p_driver,
                         const void *                p_config,
                         size_t                      config_size)
{
    drvreg_registry_t registry = {0};
    if ((0 != drvreg_init(&registry, p_descriptor, 1U)) ||
        (0 != drvreg_bind(&registry,
                          p_descriptor->p_driver_id,
                          p_settings->source.name,
                          p_driver,
                          sizeof(p_slot->context),
                          &p_slot->component)))
    {
        return -1;
    }
    p_slot->b_touched = true;
    if (p_slot->b_display)
    {
        const int result = open_display(p_slot, p_settings);
        if (0 != result)
        {
            return result;
        }
    }
    if (p_slot->b_light)
    {
        const int result =
            p_slot->p_owner->p_io->p_line_open(p_settings->line_slot,
                                               p_settings->enable_pin,
                                               false,
                                               &p_slot->context.light.enable);
        if (0 != result)
        {
            return result;
        }
    }
    const int result =
        component_configure(&p_slot->component, p_config, config_size);
    return ((0 == result) && (!p_slot->b_display) && (!p_slot->b_output))
               ? shared_expect(&p_settings->source)
               : result;
}

/** @brief Dispatch one bounded typed sample operation through its adapter. */
static int step (void * p_context)
{
    cfgbind_slot_t * p_slot = p_context;
    if ((NULL == p_slot) || (!p_slot->b_touched))
    {
        return -1;
    }
    if (p_slot->b_output)
    {
        return output_step(p_slot);
    }
    return p_slot->b_display ? display_step(&p_slot->context.display)
                             : p_slot->p_step(p_slot->component.p_context);
}

/** @brief Preserve the active union member until its cleanup succeeds. */
static int cleanup (void * p_context)
{
    cfgbind_slot_t * p_slot = p_context;
    if (NULL == p_slot)
    {
        return -1;
    }
    if (!p_slot->b_touched)
    {
        return 0;
    }
    int result = p_slot->p_cleanup(p_slot->component.p_context);
    if ((0 == result) && (p_slot->b_display))
    {
        result = close_display(p_slot);
    }
    if ((0 == result) && (p_slot->b_light))
    {
        result = close_light(p_slot);
    }
    if (0 == result)
    {
        p_slot->b_touched = false;
    }
    return result;
}

/** @brief Copy admitted scalar fields into the existing driver configuration.
 */
static void settings (const cfgplan_t *   p_plan,
                      size_t              index,
                      uint8_t             pair,
                      cfgbind_setting_t * p_settings)
{
    const cfgplan_device_t * p_device = &p_plan->devices[index];
    memcpy(p_settings->source.name,
           p_device->name,
           sizeof(p_settings->source.name));
    /* Admission bounds the destination to four queues and source to eight. */
    p_settings->source.h_queue      = (msgbus_queue_id_t)p_device->destination;
    p_settings->source.source_index = (uint16_t)index;
    if (CFGPLAN_ENV == p_device->driver)
    {
        p_settings->source.type = UTHOME_SAMPLE_ENVIRONMENT;
    }
    else if (CFGPLAN_RADAR == p_device->driver)
    {
        p_settings->source.type = UTHOME_SAMPLE_TARGETS;
    }
    else if (CFGPLAN_LIGHT == p_device->driver)
    {
        p_settings->source.type = UTHOME_SAMPLE_LIGHT;
    }
    else
    {
        p_settings->source.type = UTHOME_SAMPLE_DISTANCE;
    }
    p_settings->source.stale_ms =
        (2U * p_device->period_ms) + (p_device->timeout_us / 1000U) + 100U;
    p_settings->driver = p_device->driver;
    p_settings->pair   = pair;
    if ((CFGPLAN_OLED == p_device->driver) || (CFGPLAN_PWM == p_device->driver))
    {
        output_settings(p_plan, index, p_settings);
        return;
    }
    /* Admission proved source/queue/pin fields fit these concrete APIs. */
    if (CFGPLAN_ENV == p_device->driver)
    {
        p_settings->settings.environment = (bmpinst_config_t){
            .address       = (uint8_t)p_device->address,
            .source_index  = (uint16_t)index,
            .timeout_ms    = p_device->timeout_us / 1000U,
            .h_destination = (msgbus_queue_id_t)p_device->destination};
    }
    else if (CFGPLAN_LIGHT == p_device->driver)
    {
        p_settings->enable_pin     = (uint8_t)p_device->pin_a;
        p_settings->settings.light = (seninst_config_t){
            .source_index  = (uint16_t)index,
            .timeout_ms    = p_device->timeout_us / 1000U,
            .h_destination = (msgbus_queue_id_t)p_device->destination};
    }
    else if (CFGPLAN_RADAR == p_device->driver)
    {
        const cfgplan_bus_t * p_bus = &p_plan->buses[p_device->bus];
        p_settings->settings.radar  = (ldinst_config_t){
             .uart          = {.baud            = p_bus->clock,
                               .rx_buffer_bytes = (uint16_t)p_bus->buffer_bytes,
                               .port            = (uint8_t)p_bus->port,
                               .tx_gpio         = (uint8_t)p_bus->pin_b,
                               .rx_gpio         = (uint8_t)p_bus->pin_a,
                               .data_bits       = (uint8_t)p_bus->data_bits,
                               .stop_bits       = (uint8_t)p_bus->stop_bits,
                               .parity          = (uint8_t)p_bus->parity},
             .timeout_ms    = p_device->timeout_us / 1000U,
             .source_index  = (uint16_t)index,
             .h_destination = (msgbus_queue_id_t)p_device->destination};
    }
    else
    {
        p_settings->settings.distance = (hcinst_config_t){
            .pulse         = {.trigger_gpio = (uint8_t)p_device->pin_a,
                              .input_gpio   = (uint8_t)p_device->pin_b},
            .source_index  = (uint16_t)index,
            .timeout_us    = p_device->timeout_us,
            .h_destination = (msgbus_queue_id_t)p_device->destination};
    }
}

/** @brief Copy output-only settings without assigning sensor queue ownership.
 */
static void output_settings (const cfgplan_t *   p_plan,
                             size_t              index,
                             cfgbind_setting_t * p_settings)
{
    const cfgplan_device_t * p_device = &p_plan->devices[index];
    if (CFGPLAN_OLED == p_device->driver)
    {
        const cfgplan_bus_t * p_bus  = &p_plan->buses[p_device->bus];
        p_settings->settings.display = (cfgbind_panel_t){
            .panel     = {.timeout_ms       = p_device->timeout_us / 1000U,
                          .setup_timeout_ms = 100U,
                          .height           = 64U,
                          .contrast         = 0x7FU,
                          .b_charge_pump    = true},
            .spi       = {.cs_gpio  = (uint8_t)p_device->pin_a,
                          .clock_hz = p_bus->clock},
            .data_pin  = (uint8_t)p_device->pin_b,
            .reset_pin = (uint8_t)p_device->pin_c,
            .sources   = (uint8_t)p_device->consumers};
    }
    else
    {
        p_settings->settings.output.pwm = (pwmio_config_t){
            .gpio =
                (uint8_t)p_device->pin_a, /* Admitted C3 GPIO fits a byte. */
            .frequency_hz = p_device->frequency_hz};
        for (size_t rule = 0U; rule < p_plan->rule_count; ++rule)
        {
            if (index == p_plan->rules[rule].output_index)
            {
                p_settings->settings.output.rule = p_plan->rules[rule].config;
            }
        }
    }
}

/** @brief Construct a copied plan without writing the currently live slots. */
static bool make_plan (cfgbind_state_t * p_state)
{
    if ((sizeof(cfgbind_setting_t) > UTHOME_ACTIVATE_CONFIG_MAX) ||
        (sizeof(cfgbind_resource_t) > UTHOME_ACTIVATE_CONFIG_MAX))
    {
        return false;
    }
    const cfgplan_t * p_candidate = &p_state->candidate;
    activate_plan_t * p_plan      = &p_state->plan;
    memset(p_plan, 0, sizeof(*p_plan));
    p_plan->device_count = p_candidate->device_count;
    p_plan->queue_count  = p_candidate->queue_count;
    const size_t sizes[] = {0U,
                            sizeof(sample_environment_t),
                            sizeof(sample_target_report_t),
                            sizeof(sample_distance_t),
                            sizeof(sample_health_t),
                            sizeof(sample_light_t)};
    for (size_t index = 0U; index < p_plan->queue_count; ++index)
    {
        const cfgplan_queue_t * p_queue = &p_candidate->queues[index];
        p_plan->queues[index]           = (msgbus_queue_config_t){
                      .type            = (uint16_t)p_queue->type,
                      .item_size       = sizes[p_queue->type],
                      .depth           = (uint8_t)p_queue->depth,
                      .overflow_policy = UTHOME_MSGBUS_DROP_OLDEST};
    }
    cfgbind_resource_t resource = {.health_ms = p_candidate->health_ms};
    for (uint8_t index = 0U; index < p_plan->queue_count; ++index)
    {
        if (UTHOME_SAMPLE_HEALTH == p_plan->queues[index].type)
        {
            /* At most four bits are set in this eight-bit queue mask. */
            resource.health_queues |= (uint8_t)(UINT32_C(1) << index);
        }
    }
    uint8_t pair           = 0U;
    uint8_t display_pair   = 0U;
    uint8_t output_channel = 0U;
    uint8_t line_slot      = 0U;
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        const cfgplan_device_t * p_device = &p_candidate->devices[index];
        activate_device_t *      p_bound  = &p_plan->devices[index];
        cfgbind_setting_t        config   = {0};
        settings(
            p_candidate,
            index,
            (CFGPLAN_OLED == p_device->driver)
                ? display_pair++
                : ((CFGPLAN_PWM == p_device->driver) ? output_channel++ : pair),
            &config);
        if ((CFGPLAN_OLED == p_device->driver) ||
            (CFGPLAN_LIGHT == p_device->driver))
        {
            config.line_slot = line_slot;
            line_slot += (CFGPLAN_OLED == p_device->driver) ? 2U : 1U;
            if (line_slot > 8U)
            {
                return false;
            }
        }
        if ((CFGPLAN_OLED == p_device->driver) && (p_device->b_enabled))
        {
            const cfgplan_bus_t * p_bus = &p_candidate->buses[p_device->bus];
            resource.b_spi              = true;
            /* Admission already restricts SPI2 and the valid C3 pin set. */
            resource.mosi = (uint8_t)p_bus->pin_a;
            resource.sclk = (uint8_t)p_bus->pin_b;
        }
        if (CFGPLAN_DISTANCE == p_device->driver)
        {
            ++pair;
        }
        if (((CFGPLAN_ENV == p_device->driver) ||
             (CFGPLAN_LIGHT == p_device->driver)) &&
            (p_device->b_enabled))
        {
            const cfgplan_bus_t * p_bus = &p_candidate->buses[p_device->bus];
            resource.b_i2c              = true;
            resource.i2c = (i2cbus_config_t){.port     = (uint8_t)p_bus->port,
                                             .sda_gpio = (uint8_t)p_bus->pin_a,
                                             .scl_gpio = (uint8_t)p_bus->pin_b,
                                             .clock_hz = p_bus->clock};
        }
        memcpy(p_bound->component.name,
               p_device->name,
               sizeof(p_bound->component.name));
        p_bound->component.p_context    = &p_state->slots[index];
        p_bound->component.p_operations = &g_device_ops;
        p_bound->worker =
            (rt_port_worker_t){.p_step    = step,
                               .p_cleanup = cleanup,
                               .p_context = &p_state->slots[index]};
        p_bound->task =
            (rt_port_task_config_t){.priority = (uint8_t)p_device->priority,
                                    .stack_size_bytes = p_device->stack_bytes,
                                    .period_ms        = p_device->period_ms};
        p_bound->heartbeat_ms =
            (2U * p_device->period_ms) + (p_device->timeout_us / 1000U) + 100U;
        /* Keep the current conservative single-consumer admission contract,
         * although this display reads snapshots without removing queue data. */
        p_bound->consumer_mask = (uint8_t)p_device->consumers;
        p_bound->b_enabled     = p_device->b_enabled;
        p_bound->config_size   = sizeof(config);
        memcpy(p_bound->config, &config, sizeof(config));
    }
    p_plan->resources.component.p_context    = p_state;
    p_plan->resources.component.p_operations = &g_resource_ops;
    p_plan->resources.p_release              = close_resources;
    p_plan->resources.config_size            = sizeof(resource);
    memcpy(p_plan->resources.config, &resource, sizeof(resource));
    return true;
}

/** @brief Retain every acquired resource until checked reverse teardown. */
static int open_display (cfgbind_slot_t *          p_slot,
                         const cfgbind_setting_t * p_config)
{
    cfgbind_display_t *     p_display = &p_slot->context.display;
    const cfgbind_panel_t * p_panel   = &p_config->settings.display;
    const cfgbind_io_t *    p_io      = p_slot->p_owner->p_io;
    p_display->instance.p_spi         = &p_display->device;
    p_display->sources                = p_panel->sources;
    /* Four admitted displays need at most eight independent control lines. */
    const uint8_t line_slot = p_config->line_slot;
    int           result    = spibus_attach(
        p_slot->p_owner->p_spi, &p_display->device, &p_panel->spi);
    if (0 == result)
    {
        result = p_io->p_line_open(line_slot,
                                   p_panel->data_pin,
                                   false,
                                   &p_display->instance.data_line);
    }
    if (0 == result)
    {
        result = p_io->p_line_open((uint8_t)(line_slot + 1U),
                                   p_panel->reset_pin,
                                   true,
                                   &p_display->instance.reset);
    }
    return result;
}

/** @brief Release an enable line only after the sensor releases the bus. */
static int close_light (cfgbind_slot_t * p_slot)
{
    digiout_t * p_enable = &p_slot->context.light.enable;
    return (NULL == p_enable->p_set)
               ? 0
               : p_slot->p_owner->p_io->p_line_close(p_enable);
}

/** @brief Called only after display-off succeeded or no display was claimed. */
static int close_display (cfgbind_slot_t * p_slot)
{
    cfgbind_display_t *  p_display = &p_slot->context.display;
    const cfgbind_io_t * p_io      = p_slot->p_owner->p_io;
    int                  result    = 0;
    if (NULL != p_display->instance.reset.p_set)
    {
        result = p_io->p_line_close(&p_display->instance.reset);
    }
    if ((0 == result) && (NULL != p_display->instance.data_line.p_set))
    {
        result = p_io->p_line_close(&p_display->instance.data_line);
    }
    if ((0 == result) && (NULL != p_display->device.p_bus))
    {
        result = spibus_detach(&p_display->device);
    }
    return result;
}

/** @brief Render one copied observation, releasing the guard before SPI I/O. */
static int display_step (cfgbind_display_t * p_display)
{
    sampcache_entry_t entry  = {0};
    uint64_t          now_ms = 0U;
    const int result = shared_sample(p_display->sources, &entry, &now_ms);
    /* Contention shows unavailable data and retries on the next worker period;
     * a previous FRESH label must not remain indefinitely on a blocked reader.
     */
    if (0 != oledview_render(&p_display->instance.display,
                             (0 == result) ? &entry : NULL,
                             now_ms))
    {
        return SPIBUS_STATE;
    }
    return oledinst_step(&p_display->instance);
}

/** @brief One worker evaluates its rule and writes one typed output command. */
static int output_step (cfgbind_slot_t * p_slot)
{
    cfgbind_output_t * p_output = &p_slot->context.output;
    sampcache_entry_t  entry    = {0};
    uint64_t           now_ms   = 0U;
    const int          sampled =
        shared_source(p_output->rule.source_index, &entry, &now_ms);
    if ((0 != sampled) && (0 != rt_port_now_ms(&now_ms)))
    {
        now_ms =
            0U; /* Regression/unavailable sample forces the zero command. */
    }
    pwminst_command_t command   = {0};
    const int         evaluated = rulecalc_step(&p_output->rule,
                                        &p_output->state,
                                        (0 == sampled) ? &entry : NULL,
                                        now_ms,
                                        &command.duty_permille);
    int               result    = 0;
    if (command.duty_permille != p_output->instance.p_port->duty_permille)
    {
        result = component_write(&p_slot->component, &command, sizeof(command));
    }
    if ((0 != evaluated) || (0 != result))
    {
        command.duty_permille = 0U;
        const int stopped =
            component_write(&p_slot->component, &command, sizeof(command));
        /* The worker's failure and retained ownership remain observable even
         * if concurrent health accounting is busy. Cleanup must still retry. */
        (void)shared_report(UTHOME_HEALTH_DEVICE_IO);
        return (0 != stopped) ? stopped : -1;
    }
    return 0;
}

/* End of file. */

