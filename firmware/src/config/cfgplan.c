/**
 * @file cfgplan.c
 * @brief Admit common fields and reserve every configured physical connection.
 */
#include "cfgplan.h"
#include "cfgdev.h"
#include "cfgrule.h"
#include <string.h>
static bool common (const cfgjson_value_t * p_root, cfgplan_t * p_plan);
static bool buses (const cfgjson_value_t * p_array, cfgplan_t * p_plan);
static bool bus (const cfgjson_value_t * p_value, cfgplan_bus_t * p_bus);
static bool queues (const cfgjson_value_t * p_array, cfgplan_t * p_plan);
static bool wiring (const cfgplan_t * p_plan);
static bool claim (uint32_t * p_claims, uint32_t pin, bool b_uart);

/** @brief Independently admit the complete candidate before any activation. */
int cfgplan_read (const uint8_t * p_data,
                  size_t          size,
                  uint32_t        compiled_mask,
                  cfgplan_t *     p_candidate)
{
    if (NULL == p_candidate)
    {
        return -1;
    }
    memset(p_candidate, 0, sizeof(*p_candidate));
    const cfgjson_value_t * p_root = cfgjson_open(p_data, size);
    if (NULL == p_root)
    {
        return -1;
    }
    bool b_ok = (common(p_root, p_candidate)) &&
                (buses(cfgjson_get(p_root, "interfaces"), p_candidate)) &&
                (queues(cfgjson_get(p_root, "queues"), p_candidate));
    const cfgjson_value_t * p_devices = cfgjson_get(p_root, "devices");
    b_ok                              = b_ok &&
           (cfgjson_array(
               p_devices, UTHOME_PLAN_DEVICES, &p_candidate->device_count));
    for (size_t index = 0U; b_ok && (index < p_candidate->device_count);
         ++index)
    {
        b_ok = cfgdev_read(
            cfgjson_at(p_devices, index), p_candidate, index, compiled_mask);
    }
    b_ok = b_ok && (cfgdev_limits(p_candidate)) && (wiring(p_candidate)) &&
           (cfgrule_read(p_root, p_candidate));
    cfgjson_close();
    if (!b_ok)
    {
        memset(p_candidate, 0, sizeof(*p_candidate));
        return -1;
    }
    return 0;
}

/** @brief Check the exact node, board and runtime-policy versions. */
static bool common (const cfgjson_value_t * p_root, cfgplan_t * p_plan)
{
    static const char * const p_keys[]       = {"schema_version",
                                                "node_id",
                                                "board",
                                                "runtime",
                                                "interfaces",
                                                "queues",
                                                "devices",
                                                "rules"};
    static const char * const p_board_keys[] = {
        "platform", "variant", "capability_version"};
    static const char * const p_run_keys[] = {"health_report_interval_ms",
                                              "max_device_tasks",
                                              "queue_depth",
                                              "configuration_source",
                                              "rollback_on_error"};
    const cfgjson_value_t *   p_board      = cfgjson_get(p_root, "board");
    const cfgjson_value_t *   p_run        = cfgjson_get(p_root, "runtime");
    bool                      b_rollback   = false;
    return (cfgjson_object(p_root, p_keys, 8U, 127U)) &&
           ((cfgjson_is(cfgjson_get(p_root, "schema_version"), "2.0.0")) ||
            (cfgjson_is(cfgjson_get(p_root, "schema_version"), "2.1.0"))) &&
           (cfgjson_name(cfgjson_get(p_root, "node_id"), p_plan->name)) &&
           (cfgjson_object(p_board, p_board_keys, 3U, 7U)) &&
           (cfgjson_is(cfgjson_get(p_board, "platform"), "esp32-c3")) &&
           (cfgjson_is(cfgjson_get(p_board, "variant"),
                       "esp32-c3-supermini")) &&
           (cfgjson_is(cfgjson_get(p_board, "capability_version"), "1.0.0")) &&
           (cfgjson_object(p_run, p_run_keys, 5U, 31U)) &&
           (cfgjson_uint(cfgjson_get(p_run, "health_report_interval_ms"),
                         100U,
                         60000U,
                         &p_plan->health_ms)) &&
           (cfgjson_uint(cfgjson_get(p_run, "max_device_tasks"),
                         1U,
                         4U,
                         &p_plan->max_tasks)) &&
           (cfgjson_uint(cfgjson_get(p_run, "queue_depth"),
                         1U,
                         8U,
                         &p_plan->max_depth)) &&
           (cfgjson_is(cfgjson_get(p_run, "configuration_source"),
                       "usb_serial")) &&
           (cfgjson_bool(cfgjson_get(p_run, "rollback_on_error"),
                         &b_rollback)) &&
           b_rollback;
}

/** @brief Decode three concrete controller contracts, without opening them. */
static bool bus (const cfgjson_value_t * p_value, cfgplan_bus_t * p_bus)
{
    static const char * const p_i2c_keys[] = {
        "id", "protocol", "port", "sda_gpio", "scl_gpio", "clock_hz"};
    static const char * const p_spi_keys[] = {
        "id", "protocol", "host", "mosi_gpio", "sclk_gpio", "clock_hz"};
    static const char * const p_uart_keys[] = {"id",
                                               "protocol",
                                               "port",
                                               "rx_gpio",
                                               "tx_gpio",
                                               "baud_rate",
                                               "data_bits",
                                               "parity",
                                               "stop_bits",
                                               "rx_buffer_bytes"};
    const cfgjson_value_t *   p_kind        = cfgjson_get(p_value, "protocol");
    if (!(cfgjson_name(cfgjson_get(p_value, "id"), p_bus->name)))
    {
        return false;
    }
    if ((cfgjson_is(p_kind, "i2c")))
    {
        p_bus->kind = CFGPLAN_I2C;
        return (cfgjson_object(p_value, p_i2c_keys, 6U, 63U)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "port"), 0U, 0U, &p_bus->port)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "sda_gpio"), 0U, 21U, &p_bus->pin_a)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "scl_gpio"), 0U, 21U, &p_bus->pin_b)) &&
               (cfgjson_uint(cfgjson_get(p_value, "clock_hz"),
                             10000U,
                             400000U,
                             &p_bus->clock));
    }
    if ((cfgjson_is(p_kind, "spi")))
    {
        p_bus->kind = CFGPLAN_SPI;
        return (cfgjson_object(p_value, p_spi_keys, 6U, 63U)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "host"), 2U, 2U, &p_bus->port)) &&
               (cfgjson_uint(cfgjson_get(p_value, "mosi_gpio"),
                             0U,
                             21U,
                             &p_bus->pin_a)) &&
               (cfgjson_uint(cfgjson_get(p_value, "sclk_gpio"),
                             0U,
                             21U,
                             &p_bus->pin_b)) &&
               (cfgjson_uint(cfgjson_get(p_value, "clock_hz"),
                             100000U,
                             8000000U,
                             &p_bus->clock));
    }
    if ((cfgjson_is(p_kind, "uart")))
    {
        p_bus->kind                      = CFGPLAN_UART;
        const cfgjson_value_t * p_parity = cfgjson_get(p_value, "parity");
        p_bus->parity = (cfgjson_is(p_parity, "even")) ? 2U : 1U;
        if ((cfgjson_is(p_parity, "none")))
        {
            p_bus->parity = 0U;
        }
        return (cfgjson_object(p_value, p_uart_keys, 10U, 1023U)) &&
               ((cfgjson_is(p_parity, "none")) ||
                (cfgjson_is(p_parity, "even")) ||
                (cfgjson_is(p_parity, "odd"))) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "port"), 1U, 1U, &p_bus->port)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "rx_gpio"), 20U, 20U, &p_bus->pin_a)) &&
               (cfgjson_uint(
                   cfgjson_get(p_value, "tx_gpio"), 21U, 21U, &p_bus->pin_b)) &&
               (cfgjson_uint(cfgjson_get(p_value, "baud_rate"),
                             9600U,
                             256000U,
                             &p_bus->clock)) &&
               (cfgjson_uint(cfgjson_get(p_value, "data_bits"),
                             8U,
                             8U,
                             &p_bus->data_bits)) &&
               (cfgjson_uint(cfgjson_get(p_value, "stop_bits"),
                             1U,
                             2U,
                             &p_bus->stop_bits)) &&
               (cfgjson_uint(cfgjson_get(p_value, "rx_buffer_bytes"),
                             512U,
                             512U,
                             &p_bus->buffer_bytes));
    }
    return false;
}

/** @brief Reject duplicate interface names and duplicate controller owners. */
static bool buses (const cfgjson_value_t * p_array, cfgplan_t * p_plan)
{
    if (!(cfgjson_array(p_array, UTHOME_PLAN_BUSES, &p_plan->bus_count)))
    {
        return false;
    }
    for (size_t index = 0U; index < p_plan->bus_count; ++index)
    {
        cfgplan_bus_t * p_bus = &p_plan->buses[index];
        if (!bus(cfgjson_at(p_array, index), p_bus))
        {
            return false;
        }
        for (size_t old = 0U; old < index; ++old)
        {
            if ((0 == strcmp(p_bus->name, p_plan->buses[old].name)) ||
                (p_bus->kind == p_plan->buses[old].kind))
            {
                return false;
            }
        }
    }
    return true;
}

/** @brief Resolve supported copied sample p_types and bounded FIFO policy. */
static bool queues (const cfgjson_value_t * p_array, cfgplan_t * p_plan)
{
    static const char * const p_keys[] = {
        "id", "item_type", "depth", "overflow_policy"};
    static const char * const p_types[] = {"environment_sample",
                                           "target_report",
                                           "distance_sample",
                                           "health_event"};
    if (!(cfgjson_array(p_array, UTHOME_PLAN_QUEUES, &p_plan->queue_count)))
    {
        return false;
    }
    for (size_t index = 0U; index < p_plan->queue_count; ++index)
    {
        cfgplan_queue_t *       p_queue = &p_plan->queues[index];
        const cfgjson_value_t * p_value = cfgjson_at(p_array, index);
        if ((!(cfgjson_object(p_value, p_keys, 4U, 15U))) ||
            (!(cfgjson_name(cfgjson_get(p_value, "id"), p_queue->name))) ||
            (!(cfgjson_uint(cfgjson_get(p_value, "depth"),
                            1U,
                            p_plan->max_depth,
                            &p_queue->depth))) ||
            (!(cfgjson_is(cfgjson_get(p_value, "overflow_policy"),
                          "drop_oldest"))))
        {
            return false;
        }
        for (size_t type = 0U; type < 4U; ++type)
        {
            if ((cfgjson_is(cfgjson_get(p_value, "item_type"), p_types[type])))
            {
                p_queue->type = (uint32_t)type + 1U;
            } /* Four supported sample IDs. */
        }
        if (0U == p_queue->type)
        {
            return false;
        }
        for (size_t old = 0U; old < index; ++old)
        {
            if (0 == strcmp(p_queue->name, p_plan->queues[old].name))
            {
                return false;
            }
        }
    }
    return true;
}

/** @brief Reserve actual supported C3 pins, including disabled connections. */
static bool claim (uint32_t * p_claims, uint32_t pin, bool b_uart)
{
    const uint32_t allowed = b_uart ? UINT32_C(0x300000) : UINT32_C(0x4FB);
    if ((pin > 21U) || (0U == (allowed & (UINT32_C(1) << pin))) ||
        (0U != (*p_claims & (UINT32_C(1) << pin))))
    {
        return false;
    }
    *p_claims |= UINT32_C(1) << pin;
    return true;
}

/** @brief Check physical connections independently of enabled worker counts. */
static bool wiring (const cfgplan_t * p_plan)
{
    uint32_t pins = 0U;
    for (size_t index = 0U; index < p_plan->bus_count; ++index)
    {
        const cfgplan_bus_t * p_bus = &p_plan->buses[index];
        if ((!claim(&pins, p_bus->pin_a, CFGPLAN_UART == p_bus->kind)) ||
            (!claim(&pins, p_bus->pin_b, CFGPLAN_UART == p_bus->kind)))
        {
            return false;
        }
    }
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        const cfgplan_device_t * p_device = &p_plan->devices[index];
        if ((CFGPLAN_PWM == p_device->driver) &&
            (!claim(&pins, p_device->pin_a, false)))
        {
            return false;
        }
        if ((CFGPLAN_OLED == p_device->driver) ||
            (CFGPLAN_DISTANCE == p_device->driver))
        {
            if ((!claim(&pins, p_device->pin_a, false)) ||
                (!claim(&pins, p_device->pin_b, false)))
            {
                return false;
            }
            if ((CFGPLAN_OLED == p_device->driver) &&
                (!claim(&pins, p_device->pin_c, false)))
            {
                return false;
            }
        }
    }
    return true;
}

/* End of file. */

