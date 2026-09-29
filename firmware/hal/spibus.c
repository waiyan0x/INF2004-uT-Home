/**
 * @file spibus.c
 * @brief Validate SPI device ownership and finite requests before dispatch.
 */
#include "spibus.h"
static bool valid_bus (const spibus_t * p_bus);
static size_t device_slot (const spibus_device_t * p_device);

/** @brief Attach one distinct chip select without allocating storage. */
int spibus_attach (spibus_t *              p_bus,
                   spibus_device_t *       p_device,
                   const spibus_config_t * p_config)
{
    if ((!valid_bus(p_bus)) || (NULL == p_device) || (NULL == p_config) ||
        (NULL != p_device->p_bus) || (p_config->cs_gpio >= 32U) ||
        (p_config->mode > 3U) || (0U == p_config->clock_hz))
    {
        return SPIBUS_INVALID;
    }
    size_t slot = UTHOME_SPI_DEVICES;
    for (size_t index = 0U; index < UTHOME_SPI_DEVICES; ++index)
    {
        if (NULL == p_bus->p_devices[index])
        {
            slot = index;
        }
        else if (p_config->cs_gpio == p_bus->chip_selects[index])
        {
            return SPIBUS_STATE;
        }
        else
        {
            /* This slot belongs to another chip select. */
        }
    }
    if (UTHOME_SPI_DEVICES == slot)
    {
        return SPIBUS_STATE;
    }
    const int result =
        p_bus->p_operations->p_attach(p_bus->p_context, p_config, true);
    if (0 == result)
    {
        *p_device = (spibus_device_t){.p_bus = p_bus, .config = *p_config};
        p_bus->p_devices[slot]    = p_device;
        p_bus->chip_selects[slot] = p_config->cs_gpio;
    }
    return result;
}

/** @brief Keep the device registered until hardware detachment succeeds. */
int spibus_detach (spibus_device_t * p_device)
{
    const size_t slot = device_slot(p_device);
    if ((UTHOME_SPI_DEVICES == slot) || (NULL != p_device->p_owner))
    {
        return SPIBUS_STATE;
    }
    spibus_t * p_bus  = p_device->p_bus;
    const int  result = p_bus->p_operations->p_attach(
        p_bus->p_context, &p_device->config, false);
    if (0 == result)
    {
        p_bus->p_devices[slot] = NULL;
        *p_device              = (spibus_device_t){0};
    }
    return result;
}

/** @brief Reject duplicate driver contexts before resetting a device. */
int spibus_claim (spibus_device_t * p_device, const void * p_owner)
{
    if ((UTHOME_SPI_DEVICES == device_slot(p_device)) || (NULL == p_owner) ||
        (NULL != p_device->p_owner))
    {
        return SPIBUS_STATE;
    }
    p_device->p_owner = p_owner;
    return 0;
}

/** @brief Release only the driver's own claim after checked cleanup. */
int spibus_release (spibus_device_t * p_device, const void * p_owner)
{
    if ((UTHOME_SPI_DEVICES == device_slot(p_device)) || (NULL == p_owner) ||
        (p_owner != p_device->p_owner))
    {
        return SPIBUS_STATE;
    }
    p_device->p_owner = NULL;
    return 0;
}

/** @brief Validate bounds and enforce the deadline around the backend call. */
int spibus_transfer (spibus_device_t *        p_device,
                     const spibus_request_t * p_request)
{
    if ((UTHOME_SPI_DEVICES == device_slot(p_device)) || (NULL == p_request) ||
        (0U == p_request->size) || (p_request->size > UTHOME_SPI_BYTES) ||
        ((NULL == p_request->p_write) && (NULL == p_request->p_read)))
    {
        return SPIBUS_INVALID;
    }
    uint64_t now = 0U;
    if (0 != spibus_now(p_device->p_bus, &now))
    {
        return SPIBUS_IO;
    }
    if ((now >= p_request->deadline_ms) ||
        ((p_request->deadline_ms - now) > UINT32_C(2147483647)))
    {
        return SPIBUS_TIMEOUT;
    }
    const int result = p_device->p_bus->p_operations->p_transfer(
        p_device->p_bus->p_context, &p_device->config, p_request);
    if (0 != result)
    {
        return result;
    }
    if (0 != spibus_now(p_device->p_bus, &now))
    {
        return SPIBUS_IO;
    }
    return (now >= p_request->deadline_ms) ? SPIBUS_TIMEOUT : 0;
}

/** @brief Read the common monotonic time. */
int spibus_now (spibus_t * p_bus, uint64_t * p_now)
{
    if ((!valid_bus(p_bus)) || (NULL == p_now))
    {
        return SPIBUS_INVALID;
    }
    return p_bus->p_operations->p_now(p_bus->p_context, p_now);
}

/** @brief Establish one budget for a complete driver operation. */
int spibus_deadline (spibus_t * p_bus,
                     uint32_t   timeout_ms,
                     uint64_t * p_deadline)
{
    uint64_t now = 0U;
    if ((NULL == p_deadline) || (0U == timeout_ms) ||
        (timeout_ms > UINT32_C(2147483647)) || (0 != spibus_now(p_bus, &now)) ||
        (now > (UINT64_MAX - timeout_ms)))
    {
        return SPIBUS_INVALID;
    }
    *p_deadline = now + timeout_ms;
    return 0;
}

/** @brief Require a real bounded delay implementation. */
int spibus_delay (spibus_t * p_bus, uint32_t duration_ms, uint64_t deadline_ms)
{
    if (!valid_bus(p_bus))
    {
        return SPIBUS_INVALID;
    }
    return p_bus->p_operations->p_delay(
        p_bus->p_context, duration_ms, deadline_ms);
}

/** @brief Prevent bus shutdown while devices remain attached. */
bool spibus_has_devices (const spibus_t * p_bus)
{
    if (NULL != p_bus)
    {
        for (size_t index = 0U; index < UTHOME_SPI_DEVICES; ++index)
        {
            if (NULL != p_bus->p_devices[index])
            {
                return true;
            }
        }
    }
    return false;
}

/** @brief Require the complete backend contract. */
static bool valid_bus (const spibus_t * p_bus)
{
    return (NULL != p_bus) && (NULL != p_bus->p_context) &&
           (NULL != p_bus->p_operations) &&
           (NULL != p_bus->p_operations->p_attach) &&
           (NULL != p_bus->p_operations->p_transfer) &&
           (NULL != p_bus->p_operations->p_now) &&
           (NULL != p_bus->p_operations->p_delay);
}

/** @brief Match storage identity; copying a live device does not create a
 * handle. */
static size_t device_slot (const spibus_device_t * p_device)
{
    if ((NULL != p_device) && (valid_bus(p_device->p_bus)))
    {
        for (size_t index = 0U; index < UTHOME_SPI_DEVICES; ++index)
        {
            if (p_device == p_device->p_bus->p_devices[index])
            {
                return index;
            }
        }
    }
    return UTHOME_SPI_DEVICES;
}

/* End of file. */

