/**
 * @file i2cbus.c
 * @brief Validate bounded bus requests and preserve whole-operation deadlines.
 */
#include "i2cbus.h"

static bool valid_bus (const i2cbus_t * p_bus);
static bool valid_address (uint8_t address);

/** @brief Validate before invoking one serialized backend transfer. */
int i2cbus_transfer (i2cbus_t * p_bus, const i2cbus_request_t * p_request)
{
    if ((!valid_bus(p_bus)) || (NULL == p_request) ||
        (!valid_address(p_request->address)) ||
        (p_request->write_size > UTHOME_I2C_TX_MAX) ||
        (p_request->read_size > UTHOME_I2C_RX_MAX) ||
        (p_request->read_pause_ms > UTHOME_I2C_PAUSE_MAX_MS) ||
        ((0U != p_request->read_pause_ms) &&
         ((0U == p_request->write_size) || (0U == p_request->read_size))) ||
        ((0U == p_request->write_size) && (0U == p_request->read_size)) ||
        ((0U != p_request->write_size) && (NULL == p_request->p_write)) ||
        ((0U != p_request->read_size) && (NULL == p_request->p_read)))
    {
        return I2CBUS_INVALID;
    }
    uint64_t now = 0U;
    if (0 != i2cbus_now(p_bus, &now))
    {
        return I2CBUS_IO;
    }
    if (now >= p_request->deadline_ms)
    {
        return I2CBUS_TIMEOUT;
    }
    return p_bus->p_operations->p_transfer(p_bus->p_context, p_request);
}

/** @brief Read the backend's common monotonic millisecond clock. */
int i2cbus_now (i2cbus_t * p_bus, uint64_t * p_now)
{
    if ((!valid_bus(p_bus)) || (NULL == p_now))
    {
        return I2CBUS_INVALID;
    }
    return p_bus->p_operations->p_now(p_bus->p_context, p_now);
}

/** @brief Establish one bounded deadline for all calls in an operation. */
int i2cbus_deadline (i2cbus_t * p_bus,
                     uint32_t   timeout_ms,
                     uint64_t * p_deadline)
{
    uint64_t now = 0U;
    if ((NULL == p_deadline) || (0U == timeout_ms) ||
        (timeout_ms > UINT32_C(2147483647)) || (0 != i2cbus_now(p_bus, &now)) ||
        (now > (UINT64_MAX - timeout_ms)))
    {
        return I2CBUS_INVALID;
    }
    *p_deadline = now + timeout_ms;
    return 0;
}

/** @brief Require an actual delay implementation, using the same deadline. */
int i2cbus_delay (i2cbus_t * p_bus, uint32_t duration_us, uint64_t deadline_ms)
{
    if (!valid_bus(p_bus))
    {
        return I2CBUS_INVALID;
    }
    return p_bus->p_operations->p_delay(
        p_bus->p_context, duration_us, deadline_ms);
}

/** @brief Reserve an address before any device initialization can touch it. */
int i2cbus_claim (i2cbus_t * p_bus, uint8_t address)
{
    if ((!valid_bus(p_bus)) || (!valid_address(address)))
    {
        return I2CBUS_INVALID;
    }
    const unsigned mask = 1U << (address % 8U);
    if (0U != (p_bus->claims[address / 8U] & mask))
    {
        return I2CBUS_STATE;
    }
    /* The mask is one bit within the selected uint8_t claim byte. */
    p_bus->claims[address / 8U] |= (uint8_t)mask;
    return 0;
}

/** @brief Release a claim only after the device cleanup policy succeeds. */
int i2cbus_release (i2cbus_t * p_bus, uint8_t address)
{
    if ((!valid_bus(p_bus)) || (!valid_address(address)))
    {
        return I2CBUS_INVALID;
    }
    const unsigned mask = 1U << (address % 8U);
    if (0U == (p_bus->claims[address / 8U] & mask))
    {
        return I2CBUS_STATE;
    }
    /* Truncate the complemented mask deliberately to the claim byte. */
    p_bus->claims[address / 8U] &= (uint8_t)(~mask);
    return 0;
}

/** @brief Report whether the bus still has any borrowed device ownership. */
bool i2cbus_has_claims (const i2cbus_t * p_bus)
{
    if (NULL != p_bus)
    {
        for (size_t index = 0U; index < sizeof(p_bus->claims); ++index)
        {
            if (0U != p_bus->claims[index])
            {
                return true;
            }
        }
    }
    return false;
}

/** @brief Require complete backend callbacks and retained context storage. */
static bool valid_bus (const i2cbus_t * p_bus)
{
    return (NULL != p_bus) && (NULL != p_bus->p_operations) &&
           (NULL != p_bus->p_context) &&
           (NULL != p_bus->p_operations->p_transfer) &&
           (NULL != p_bus->p_operations->p_now) &&
           (NULL != p_bus->p_operations->p_delay);
}

/** @brief Exclude the I2C reserved-address ranges. */
static bool valid_address (uint8_t address)
{
    return (address >= 0x08U) && (address <= 0x77U);
}

/* End of file. */

