/**
 * @file i2cfake.c
 * @brief Model register transfers, elapsed time and injected bus failures.
 */
#include "i2cfake.h"

#include <string.h>

static int transfer (void * p_context, const i2cbus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms);

static const i2cbus_ops_t g_operations = {
    .p_transfer = transfer, .p_now = now_ms, .p_delay = delay_us};

/** @brief Start a fresh deterministic bus with no devices attached. */
void i2cfake_init (i2cfake_t * p_fake)
{
    if (NULL != p_fake)
    {
        memset(p_fake, 0, sizeof(*p_fake));
        p_fake->bus.p_context    = p_fake;
        p_fake->bus.p_operations = &g_operations;
    }
}

/** @brief Seed analytic coefficients: pressure=base*8 Pa, temperature=8 C. */
void i2cfake_sensor (i2cfake_t * p_fake, size_t index, uint16_t pressure_base)
{
    if ((NULL != p_fake) && (index < 2U))
    {
        memset(p_fake->registers[index], 0, sizeof(p_fake->registers[index]));
        p_fake->b_present[index]        = true;
        p_fake->registers[index][0x00U] = 0x50U;
        p_fake->registers[index][0x03U] = 0x70U;
        p_fake->registers[index][0x34U] = 0x04U;
        p_fake->registers[index][0x37U] = 0x40U;
        p_fake->registers[index][0x39U] = 0x40U;
        /* Explicit little-endian coefficient encoding, not native structs. */
        p_fake->registers[index][0x3CU] = (uint8_t)(pressure_base & 0xFFU);
        p_fake->registers[index][0x3DU] = (uint8_t)(pressure_base >> 8U);
        p_fake->registers[index][0x09U] = 0x80U;
    }
}

/** @brief Copy register bytes only if the entire bounded transfer succeeds. */
static int transfer (void * p_context, const i2cbus_request_t * p_request)
{
    i2cfake_t * p_fake = p_context;
    ++p_fake->transfers;
    if ((p_fake->now_ms >= p_request->deadline_ms) ||
        (p_fake->transfer_ms >= (p_request->deadline_ms - p_fake->now_ms)))
    {
        p_fake->now_ms = p_request->deadline_ms;
        return I2CBUS_TIMEOUT;
    }
    p_fake->now_ms += p_fake->transfer_ms;
    if (p_request->read_pause_ms >= (p_request->deadline_ms - p_fake->now_ms))
    {
        p_fake->now_ms = p_request->deadline_ms;
        return I2CBUS_TIMEOUT;
    }
    p_fake->now_ms += p_request->read_pause_ms;
    if (p_fake->transfers == p_fake->fail_at)
    {
        return p_fake->error;
    }
    if (0x4AU == p_request->address)
    {
        if (!p_fake->b_light_present)
        {
            return I2CBUS_NACK;
        }
        if ((1U != p_request->write_size) || (0U != p_request->p_write[0]) ||
            (4U != p_request->read_size) || (10U != p_request->read_pause_ms))
        {
            return I2CBUS_INVALID;
        }
        memcpy(
            p_request->p_read, p_fake->light_data, sizeof(p_fake->light_data));
        return 0;
    }
    if ((p_request->address < 0x76U) || (p_request->address > 0x77U))
    {
        return I2CBUS_NACK;
    }
    const size_t index = p_request->address - 0x76U;
    if (!p_fake->b_present[index])
    {
        return I2CBUS_NACK;
    }
    if (0U != p_request->write_size)
    {
        p_fake->position[index] = p_request->p_write[0];
        for (size_t byte = 1U; byte < p_request->write_size; ++byte)
        {
            p_fake->registers[index][p_fake->position[index]] =
                p_request->p_write[byte];
            ++p_fake->position[index];
            ++p_fake->writes;
        }
    }
    for (size_t byte = 0U; byte < p_request->read_size; ++byte)
    {
        p_request->p_read[byte] =
            p_fake->registers[index][p_fake->position[index]];
        ++p_fake->position[index];
    }
    return 0;
}

/** @brief Return the deterministic clock without advancing it. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    const i2cfake_t * p_fake = p_context;
    *p_now                   = p_fake->now_ms;
    return 0;
}

/** @brief Model minimum delay rounded up to milliseconds, within budget. */
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms)
{
    i2cfake_t * p_fake = p_context;
    ++p_fake->delays;
    const uint64_t duration = ((uint64_t)duration_us + 999U) / 1000U;
    if ((p_fake->now_ms >= deadline_ms) ||
        (duration >= (deadline_ms - p_fake->now_ms)))
    {
        p_fake->now_ms = deadline_ms;
        return I2CBUS_TIMEOUT;
    }
    p_fake->now_ms += duration;
    return 0;
}

/* End of file. */

