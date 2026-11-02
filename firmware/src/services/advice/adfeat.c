/**
 * @file adfeat.c
 * @brief Reconstruct configuration-only features without client-supplied
 * data.
 */
#include "adfeat.h"

#include <stdbool.h>

static bool add_device (cfgplan_t const *        p_plan,
                        cfgplan_device_t const * p_device,
                        uint32_t                 p_features[ADFEAT_COUNT]);
static bool within_limits (uint32_t const p_features[ADFEAT_COUNT]);

/** @brief Publish only a complete vector; partial failures leave all zeroes. */
int adfeat_build (cfgplan_t const * p_plan,
                  size_t            target,
                  uint32_t          goal_ms,
                  uint32_t          p_features[ADFEAT_COUNT])
{
    if (NULL == p_features)
    {
        return -1;
    }
    for (size_t index = 0U; index < ADFEAT_COUNT; ++index)
    {
        p_features[index] = 0U;
    }
    if ((NULL == p_plan) || (UTHOME_PLAN_DEVICES < p_plan->device_count) ||
        (UTHOME_PLAN_BUSES < p_plan->bus_count) ||
        (target >= p_plan->device_count) || (0U != p_plan->rule_count))
    {
        return -1;
    }
    cfgplan_device_t const * p_target = &p_plan->devices[target];
    if ((!p_target->b_enabled) || (CFGPLAN_ENV != p_target->driver))
    {
        return -1;
    }
    uint32_t values[ADFEAT_COUNT] = {0U};
    values[1]                     = p_target->period_ms;
    values[2]                     = goal_ms;
    values[5]                     = p_target->timeout_us;
    values[6]                     = p_target->priority;
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        cfgplan_device_t const * p_device = &p_plan->devices[index];
        if (p_device->b_enabled)
        {
            ++values[3];
            if (p_device->priority < p_target->priority)
            {
                ++values[7];
            }
            if (!add_device(p_plan, p_device, values))
            {
                return -1;
            }
        }
    }
    if ((p_plan->active_tasks != values[3]) || (!within_limits(values)))
    {
        return -1;
    }
    for (size_t index = 0U; index < ADFEAT_COUNT; ++index)
    {
        p_features[index] = values[index];
    }
    return 0;
}

/** @brief Only compiled, admitted protocol/execution shapes have semantics. */
static bool add_device (cfgplan_t const *        p_plan,
                        cfgplan_device_t const * p_device,
                        uint32_t                 p_features[ADFEAT_COUNT])
{
    if ((p_device->bus >= p_plan->bus_count) || (5U > p_device->priority) ||
        (19U < p_device->priority))
    {
        return false;
    }
    cfgplan_bus_t const * p_bus = &p_plan->buses[p_device->bus];
    size_t                group = 0U;
    switch (p_device->driver)
    {
    case CFGPLAN_ENV:
        if (CFGPLAN_I2C != p_bus->kind)
        {
            return false;
        }
        group = 8U;
    break;

    case CFGPLAN_OLED:
        if (CFGPLAN_SPI != p_bus->kind)
        {
            return false;
        }
        group = 11U;
    break;

    case CFGPLAN_RADAR:
        if ((CFGPLAN_UART != p_bus->kind) || (0U == p_bus->buffer_bytes) ||
            (65536U < p_bus->buffer_bytes))
        {
            return false;
        }
        group = 14U;
    break;

    default:
        return false;
    break;
    }
    if ((0U == p_bus->clock) || ((4000000U < p_bus->clock) && (14U == group)) ||
        ((16000000U < p_bus->clock) && (11U == group)) ||
        ((1000000U < p_bus->clock) && (8U == group)))
    {
        return false;
    }
    ++p_features[group];
    if ((0U == p_features[group + 1U]) ||
        (p_bus->clock < p_features[group + 1U]))
    {
        p_features[group + 1U] = p_bus->clock;
    }
    if (14U == group)
    {
        /* cfgdev admits this descriptor only with 30 bytes at 10 reports/s.
         * This is declared traffic, never baud interpreted as continuous data.
         */
        p_features[16] += 300U;
        if ((0U == p_features[17]) || (p_bus->buffer_bytes < p_features[17]))
        {
            p_features[17] = p_bus->buffer_bytes;
        }
    }
    else
    {
        if ((0U == p_device->period_ms) || (60000U < p_device->period_ms))
        {
            return false;
        }
        p_features[group + 2U] +=
            (1000000U + p_device->period_ms - 1U) / p_device->period_ms;
    }
    return true;
}

/** @brief Mirror the versioned Python contract; no learned outcomes enter. */
static bool within_limits (uint32_t const p_features[ADFEAT_COUNT])
{
    static uint32_t const minimum[ADFEAT_COUNT] = {
        0U, 1U, 1U, 1U, 0U, 1U, 5U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    static uint32_t const maximum[ADFEAT_COUNT] = {1U,
                                                   60000U,
                                                   60000U,
                                                   8U,
                                                   60000U,
                                                   1000000U,
                                                   19U,
                                                   8U,
                                                   8U,
                                                   1000000U,
                                                   8000000U,
                                                   8U,
                                                   16000000U,
                                                   8000000U,
                                                   8U,
                                                   4000000U,
                                                   16000000U,
                                                   65536U};
    for (size_t index = 0U; index < ADFEAT_COUNT; ++index)
    {
        if ((p_features[index] < minimum[index]) ||
            (p_features[index] > maximum[index]))
        {
            return false;
        }
    }
    return true;
}

/* End of file. */

