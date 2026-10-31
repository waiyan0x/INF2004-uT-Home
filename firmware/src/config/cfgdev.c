/**
 * @file cfgdev.c
 * @brief Translate reviewed descriptor versions and enforce route resources.
 */
#include "cfgdev.h"
#include <string.h>
static bool connection (const cfgjson_value_t * p_value,
                        const cfgplan_t *       p_plan,
                        cfgplan_device_t *      p_device);
static bool behaviour (const cfgjson_value_t * p_value,
                       const cfgplan_t *       p_plan,
                       cfgplan_device_t *      p_device);
static bool output (const cfgjson_value_t * p_value,
                    cfgplan_device_t *      p_device);
static bool radar_frame (const cfgjson_value_t * p_value);
static bool destination (const cfgjson_value_t * p_value,
                         const cfgplan_t *       p_plan,
                         cfgplan_device_t *      p_device);
static bool sources (const cfgjson_value_t * p_value,
                     const cfgplan_t *       p_plan,
                     cfgplan_device_t *      p_device);
static const char * const gp_names[] = {
    "bmp388", "ssd1306_spi", "hlk_ld2450", "hc_sr04", "pwm_output", "sen0390"};
static const uint32_t g_min_stacks[] = {
    3072U, 3072U, 4096U, 3072U, 3072U, 3072U};
static const uint32_t g_max_instances[] = {2U, 4U, 1U, 2U, 4U, 1U};

/** @brief Return the compiled descriptor name, separate from runtime code. */
const char * cfgdev_name (cfgplan_driver_t driver)
{
    return (driver < CFGPLAN_ENV) || (driver >= CFGPLAN_DRIVER_COUNT)
               ? NULL
               : gp_names[driver];
}

/** @brief Require exact descriptor properties before resolving settings. */
bool cfgdev_read (const cfgjson_value_t * p_value,
                  cfgplan_t *             p_plan,
                  size_t                  index,
                  uint32_t                compiled_mask)
{
    static const char * const p_keys[]      = {"device_id",
                                               "driver",
                                               "descriptor",
                                               "enabled",
                                               "interface",
                                               "behaviour",
                                               "task",
                                               "display"};
    static const char * const p_ref_keys[]  = {"id", "version"};
    static const char * const p_task_keys[] = {
        "name", "priority", "stack_size_bytes"};
    static const char * const p_display_keys[] = {"width_px", "height_px"};
    cfgplan_device_t *        p_device         = &p_plan->devices[index];
    const cfgjson_value_t *   p_ref  = cfgjson_get(p_value, "descriptor");
    const cfgjson_value_t *   p_task = cfgjson_get(p_value, "task");
    if ((!(cfgjson_object(p_value, p_keys, 8U, 127U))) ||
        (!(cfgjson_name(cfgjson_get(p_value, "device_id"), p_device->name))) ||
        (!(cfgjson_bool(cfgjson_get(p_value, "enabled"),
                        &p_device->b_enabled))) ||
        (!(cfgjson_object(p_ref, p_ref_keys, 2U, 3U))) ||
        (!(cfgjson_is(cfgjson_get(p_ref, "version"), "1.0.0"))))
    {
        return false;
    }
    size_t kind = 0U;
    while ((kind < CFGPLAN_DRIVER_COUNT) &&
           (!(cfgjson_is(cfgjson_get(p_value, "driver"), gp_names[kind]))))
    {
        ++kind;
    }
    if ((kind >= CFGPLAN_DRIVER_COUNT) ||
        (0U == (compiled_mask & (UINT32_C(1) << kind))) ||
        (!(cfgjson_is(cfgjson_get(p_ref, "id"), gp_names[kind]))))
    {
        return false;
    }
    p_device->driver =
        (cfgplan_driver_t)kind; /* Table index is a checked enum. */
    if ((!(cfgjson_object(p_task, p_task_keys, 3U, 7U))) ||
        (!(cfgjson_name(cfgjson_get(p_task, "name"), p_device->task_name))) ||
        (!(cfgjson_uint(
            cfgjson_get(p_task, "priority"), 5U, 19U, &p_device->priority))) ||
        (!(cfgjson_uint(cfgjson_get(p_task, "stack_size_bytes"),
                        g_min_stacks[kind],
                        8192U,
                        &p_device->stack_bytes))) ||
        (0U != (p_device->stack_bytes % 256U)))
    {
        return false;
    }
    const cfgjson_value_t * p_display = cfgjson_get(p_value, "display");
    uint32_t                dimension = 0U;
    if (CFGPLAN_OLED == p_device->driver)
    {
        if ((!(cfgjson_object(p_display, p_display_keys, 2U, 3U))) ||
            (!(cfgjson_uint(
                cfgjson_get(p_display, "width_px"), 128U, 128U, &dimension))) ||
            (!(cfgjson_uint(
                cfgjson_get(p_display, "height_px"), 64U, 64U, &dimension))))
        {
            return false;
        }
    }
    else if (NULL != p_display)
    {
        return false;
    }
    else
    { /* Non-display descriptors do not permit display settings. */
    }
    if ((!connection(cfgjson_get(p_value, "interface"), p_plan, p_device)) ||
        (!behaviour(cfgjson_get(p_value, "behaviour"), p_plan, p_device)))
    {
        return false;
    }
    if (p_device->b_enabled)
    {
        ++p_plan->active_tasks;
        p_plan->stack_bytes += p_device->stack_bytes;
    }
    return true;
}

/** @brief Enforce identity, disabled wiring, task budgets and single consumers.
 */
bool cfgdev_limits (const cfgplan_t * p_plan)
{
    uint32_t counts[CFGPLAN_DRIVER_COUNT] = {0};
    uint32_t consumers                    = 0U;
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        const cfgplan_device_t * p_device = &p_plan->devices[index];
        ++counts[p_device->driver];
        if (counts[p_device->driver] > g_max_instances[p_device->driver])
        {
            return false;
        }
        if (p_device->b_enabled)
        {
            if (0U != (consumers & p_device->consumers))
            {
                return false;
            }
            consumers |= p_device->consumers;
        }
        for (size_t old = 0U; old < index; ++old)
        {
            const cfgplan_device_t * p_old = &p_plan->devices[old];
            if ((0 == strcmp(p_old->name, p_device->name)) ||
                (0 == strcmp(p_old->task_name, p_device->task_name)))
            {
                return false;
            }
            if ((p_old->bus == p_device->bus) &&
                (p_old->driver == p_device->driver) &&
                (((CFGPLAN_ENV == p_device->driver) &&
                  (p_old->address == p_device->address)) ||
                 (CFGPLAN_RADAR == p_device->driver) ||
                 ((CFGPLAN_LIGHT == p_device->driver) &&
                  (p_old->address == p_device->address))))
            {
                return false;
            }
        }
    }
    return (p_plan->active_tasks <= p_plan->max_tasks) &&
           (p_plan->stack_bytes <= 16384U);
}

/** @brief Resolve a bus reference or a fully specified GPIO connection. */
static bool connection (const cfgjson_value_t * p_value,
                        const cfgplan_t *       p_plan,
                        cfgplan_device_t *      p_device)
{
    static const char * const p_env_keys[]   = {"ref", "address"};
    static const char * const p_light_keys[] = {
        "ref", "address", "enable_gpio"};
    static const char * const p_oled_keys[] = {
        "ref", "cs_gpio", "dc_gpio", "reset_gpio", "cs_pullup_ohms"};
    static const char * const p_radar_keys[] = {"ref"};
    static const char * const p_pulse_keys[] = {
        "protocol",
        "trigger_gpio",
        "echo_gpio",
        "echo_voltage_divider_required"};
    if (CFGPLAN_PWM == p_device->driver)
    {
        static const char * const p_keys[] = {"protocol", "output_gpio"};
        return (cfgjson_object(p_value, p_keys, 2U, 3U)) &&
               (cfgjson_is(cfgjson_get(p_value, "protocol"), "gpio")) &&
               (cfgjson_uint(cfgjson_get(p_value, "output_gpio"),
                             0U,
                             21U,
                             &p_device->pin_a));
    }
    if (CFGPLAN_DISTANCE == p_device->driver)
    {
        bool b_divider = false;
        return (cfgjson_object(p_value, p_pulse_keys, 4U, 15U)) &&
               (cfgjson_is(cfgjson_get(p_value, "protocol"), "gpio")) &&
               (cfgjson_uint(cfgjson_get(p_value, "trigger_gpio"),
                             0U,
                             21U,
                             &p_device->pin_a)) &&
               (cfgjson_uint(cfgjson_get(p_value, "echo_gpio"),
                             0U,
                             21U,
                             &p_device->pin_b)) &&
               (cfgjson_bool(
                   cfgjson_get(p_value, "echo_voltage_divider_required"),
                   &b_divider)) &&
               b_divider;
    }
    size_t bus_index = 0U;
    while ((bus_index < p_plan->bus_count) &&
           (!(cfgjson_is(cfgjson_get(p_value, "ref"),
                         p_plan->buses[bus_index].name))))
    {
        ++bus_index;
    }
    if (bus_index >= p_plan->bus_count)
    {
        return false;
    }
    p_device->bus = (uint32_t)bus_index; /* At most three interfaces. */
    const cfgplan_bus_t * p_bus = &p_plan->buses[bus_index];
    if (CFGPLAN_ENV == p_device->driver)
    {
        p_device->address =
            (cfgjson_is(cfgjson_get(p_value, "address"), "0x76")) ? 0x76U
                                                                  : 0x77U;
        return (CFGPLAN_I2C == p_bus->kind) &&
               (cfgjson_object(p_value, p_env_keys, 2U, 3U)) &&
               ((cfgjson_is(cfgjson_get(p_value, "address"), "0x76")) ||
                (cfgjson_is(cfgjson_get(p_value, "address"), "0x77")));
    }
    if (CFGPLAN_LIGHT == p_device->driver)
    {
        p_device->address = 0x4AU;
        return (CFGPLAN_I2C == p_bus->kind) &&
               (cfgjson_object(p_value, p_light_keys, 3U, 7U)) &&
               (cfgjson_is(cfgjson_get(p_value, "address"), "0x4a")) &&
               (cfgjson_uint(cfgjson_get(p_value, "enable_gpio"),
                             0U,
                             21U,
                             &p_device->pin_a));
    }
    if (CFGPLAN_RADAR == p_device->driver)
    {
        return (CFGPLAN_UART == p_bus->kind) &&
               (cfgjson_object(p_value, p_radar_keys, 1U, 1U)) &&
               (256000U == p_bus->clock) && (8U == p_bus->data_bits) &&
               (0U == p_bus->parity) && (1U == p_bus->stop_bits) &&
               (512U == p_bus->buffer_bytes);
    }
    uint32_t                pullup   = 0U;
    const cfgjson_value_t * p_pullup = cfgjson_get(p_value, "cs_pullup_ohms");
    return (CFGPLAN_SPI == p_bus->kind) &&
           (cfgjson_object(p_value, p_oled_keys, 5U, 15U)) &&
           (cfgjson_uint(
               cfgjson_get(p_value, "cs_gpio"), 0U, 21U, &p_device->pin_a)) &&
           (cfgjson_uint(
               cfgjson_get(p_value, "dc_gpio"), 0U, 21U, &p_device->pin_b)) &&
           (cfgjson_uint(cfgjson_get(p_value, "reset_gpio"),
                         0U,
                         21U,
                         &p_device->pin_c)) &&
           ((NULL == p_pullup) ||
            (cfgjson_uint(p_pullup, 1000U, 100000U, &pullup)));
}

/** @brief Validate exact constants used by the existing radar parser. */
static bool radar_frame (const cfgjson_value_t * p_value)
{
    static const char * const p_keys[] = {
        "header_hex", "footer_hex", "size_bytes", "report_rate_hz"};
    uint32_t value = 0U;
    return (cfgjson_object(p_value, p_keys, 4U, 15U)) &&
           (cfgjson_is(cfgjson_get(p_value, "header_hex"), "AAFF0300")) &&
           (cfgjson_is(cfgjson_get(p_value, "footer_hex"), "55CC")) &&
           (cfgjson_uint(
               cfgjson_get(p_value, "size_bytes"), 30U, 30U, &value)) &&
           (cfgjson_uint(
               cfgjson_get(p_value, "report_rate_hz"), 10U, 10U, &value));
}

/** @brief Resolve producer routing to a copied queue of the proper type. */
static bool destination (const cfgjson_value_t * p_value,
                         const cfgplan_t *       p_plan,
                         cfgplan_device_t *      p_device)
{
    const uint32_t types[] = {1U, 0U, 2U, 3U, 0U, 5U};
    for (size_t index = 0U; index < p_plan->queue_count; ++index)
    {
        if ((cfgjson_is(p_value, p_plan->queues[index].name)))
        {
            p_device->destination = (uint32_t)index; /* At most four queues. */
            return types[p_device->driver] == p_plan->queues[index].type;
        }
    }
    return false;
}

/** @brief Resolve unique display source queues without consuming their data. */
static bool sources (const cfgjson_value_t * p_value,
                     const cfgplan_t *       p_plan,
                     cfgplan_device_t *      p_device)
{
    size_t count = 0U;
    if ((!(cfgjson_array(p_value, 4U, &count))) || (0U == count))
    {
        return false;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        size_t queue = 0U;
        while ((queue < p_plan->queue_count) &&
               (!(cfgjson_is(cfgjson_at(p_value, index),
                             p_plan->queues[queue].name))))
        {
            ++queue;
        }
        if ((queue == p_plan->queue_count) ||
            (0U != (p_device->consumers & (UINT32_C(1) << queue))))
        {
            return false;
        }
        p_device->consumers |= UINT32_C(1) << queue;
    }
    return true;
}

/** @brief Decode driver-specific settings without adding runtime branches. */
static bool behaviour (const cfgjson_value_t * p_value,
                       const cfgplan_t *       p_plan,
                       cfgplan_device_t *      p_device)
{
    static const char * const p_env_keys[] = {
        "profile", "operation", "interval_ms", "timeout_ms", "destination"};
    static const char * const p_pulse_keys[] = {
        "profile", "operation", "interval_ms", "timeout_us", "destination"};
    static const char * const p_radar_keys[] = {
        "profile", "operation", "timeout_ms", "frame", "destination"};
    static const char * const p_oled_keys[] = {
        "profile", "operation", "refresh_interval_ms", "timeout_ms", "sources"};
    if (CFGPLAN_PWM == p_device->driver)
    {
        return output(p_value, p_device);
    }
    const cfgjson_value_t * p_profile   = cfgjson_get(p_value, "profile");
    const cfgjson_value_t * p_operation = cfgjson_get(p_value, "operation");
    bool                    b_ok        = false;
    if ((CFGPLAN_ENV == p_device->driver) ||
        (CFGPLAN_LIGHT == p_device->driver))
    {
        b_ok = (cfgjson_object(p_value, p_env_keys, 5U, 31U)) &&
               (cfgjson_is(p_profile, "periodic_sensor")) &&
               (cfgjson_is(p_operation, "read")) &&
               (cfgjson_uint(cfgjson_get(p_value, "interval_ms"),
                             (CFGPLAN_LIGHT == p_device->driver) ? 1000U : 200U,
                             60000U,
                             &p_device->period_ms)) &&
               (cfgjson_uint(cfgjson_get(p_value, "timeout_ms"),
                             (CFGPLAN_LIGHT == p_device->driver) ? 30U : 10U,
                             100U,
                             &p_device->timeout_us));
        p_device->timeout_us *= 1000U;
    }
    else if (CFGPLAN_DISTANCE == p_device->driver)
    {
        b_ok = (cfgjson_object(p_value, p_pulse_keys, 5U, 31U)) &&
               (cfgjson_is(p_profile, "periodic_sensor")) &&
               (cfgjson_is(p_operation, "measure_distance")) &&
               (cfgjson_uint(cfgjson_get(p_value, "interval_ms"),
                             100U,
                             2000U,
                             &p_device->period_ms)) &&
               (cfgjson_uint(cfgjson_get(p_value, "timeout_us"),
                             25000U,
                             30000U,
                             &p_device->timeout_us));
    }
    else if (CFGPLAN_RADAR == p_device->driver)
    {
        p_device->period_ms = 10U; /* Existing bounded polling adapter. */
        b_ok = (cfgjson_object(p_value, p_radar_keys, 5U, 31U)) &&
               (cfgjson_is(p_profile, "stream_sensor")) &&
               (cfgjson_is(p_operation, "read_frames")) &&
               (cfgjson_uint(cfgjson_get(p_value, "timeout_ms"),
                             200U,
                             500U,
                             &p_device->timeout_us)) &&
               (radar_frame(cfgjson_get(p_value, "frame")));
        p_device->timeout_us *= 1000U;
    }
    else
    {
        b_ok = (cfgjson_object(p_value, p_oled_keys, 5U, 31U)) &&
               (cfgjson_is(p_profile, "queue_display")) &&
               (cfgjson_is(p_operation, "render")) &&
               (cfgjson_uint(cfgjson_get(p_value, "refresh_interval_ms"),
                             100U,
                             1000U,
                             &p_device->period_ms)) &&
               (cfgjson_uint(cfgjson_get(p_value, "timeout_ms"),
                             5U,
                             20U,
                             &p_device->timeout_us)) &&
               (sources(cfgjson_get(p_value, "sources"), p_plan, p_device));
        p_device->timeout_us *= 1000U;
        return b_ok;
    }
    return b_ok &&
           (destination(cfgjson_get(p_value, "destination"), p_plan, p_device));
}

/** @brief Bind zero-start PWM settings; only a validated rule can command duty.
 */
static bool output (const cfgjson_value_t * p_value,
                    cfgplan_device_t *      p_device)
{
    static const char * const p_keys[] = {
        "profile", "operation", "interval_ms", "timeout_ms", "frequency_hz"};
    const bool b_ok =
        (cfgjson_object(p_value, p_keys, 5U, 31U)) &&
        (cfgjson_is(cfgjson_get(p_value, "profile"), "rule_output")) &&
        (cfgjson_is(cfgjson_get(p_value, "operation"), "set_duty")) &&
        (cfgjson_uint(cfgjson_get(p_value, "interval_ms"),
                      20U,
                      1000U,
                      &p_device->period_ms)) &&
        (cfgjson_uint(cfgjson_get(p_value, "timeout_ms"),
                      1U,
                      1U,
                      &p_device->timeout_us)) &&
        (cfgjson_uint(cfgjson_get(p_value, "frequency_hz"),
                      100U,
                      20000U,
                      &p_device->frequency_hz));
    p_device->timeout_us *= 1000U;
    return b_ok;
}

/* End of file. */

