/**
 * @file disci2c.c
 * @brief Observe approved endpoints through the existing owned bus contract.
 */
#include "disci2c.h"
static bool valid_reads (const disci2c_read_t * p_reads, size_t count);
static void observe (disci2c_t *             p_context,
                     uint64_t                deadline_ms,
                     disci2c_result_data_t * p_result);
static disci2c_observation_t
read_byte (i2cbus_t * p_bus, uint64_t deadline_ms, disci2c_record_t * p_record);

/** @brief Leave an unsuccessful close owned and available for explicit retry.
 */
int disci2c_release (disci2c_t * p_context)
{
    if ((NULL == p_context) || (NULL == p_context->p_close) ||
        (p_context->b_busy))
    {
        return -1;
    }
    int result = 0;
    if (NULL != p_context->p_bus)
    {
        result = p_context->p_close(p_context->p_bus);
        if (0 == result)
        {
            p_context->p_bus = NULL;
        }
    }
    return result;
}

/** @brief A completed observation is separate from successful identification.
 */
int disci2c_run (disci2c_t *             p_context,
                 const i2cbus_config_t * p_config,
                 const disci2c_read_t *  p_reads,
                 size_t                  count,
                 disci2c_result_data_t * p_result)
{
    if (NULL == p_result)
    {
        return -1;
    }
    *p_result = (disci2c_result_data_t){.status = DISCI2C_INVALID};
    if ((NULL == p_context) || (NULL == p_config) ||
        (NULL == p_context->p_open) || (NULL == p_context->p_close) ||
        (NULL == p_context->p_now) || (!valid_reads(p_reads, count)))
    {
        return -1;
    }
    if ((p_context->b_busy) || (NULL != p_context->p_bus))
    {
        p_result->status = DISCI2C_BUSY;
        return 0;
    }
    p_result->status = DISCI2C_FAULT;
    if ((0 != p_context->p_now(&p_result->started_ms)) ||
        (p_result->started_ms > (UINT64_MAX - 200U)))
    {
        return 0;
    }
    p_context->b_busy = true;
    p_result->count   = count;
    for (size_t index = 0U; index < count; ++index)
    {
        p_result->records[index].read   = p_reads[index];
        p_result->records[index].status = DISCI2C_NOT_CHECKED;
    }
    const int opened = p_context->p_open(p_config, &p_context->p_bus);
    if ((0 == opened) && (NULL != p_context->p_bus) &&
        (!i2cbus_has_claims(p_context->p_bus)))
    {
        p_result->status = DISCI2C_COMPLETE;
        observe(p_context, p_result->started_ms + 200U, p_result);
    }
    p_context->b_busy = false;
    if (0 != disci2c_release(p_context))
    {
        p_result->status = DISCI2C_FAULT;
    }
    uint64_t finished_ms = 0U;
    if ((0 != p_context->p_now(&finished_ms)) ||
        (finished_ms < p_result->started_ms) ||
        ((finished_ms - p_result->started_ms) > UINT32_MAX))
    {
        p_result->status = DISCI2C_FAULT;
    }
    else
    {
        /* The elapsed interval was checked to fit the wire uint32 field. */
        p_result->elapsed_ms = (uint32_t)(finished_ms - p_result->started_ms);
    }
    return 0;
}

/** @brief Reject invalid/duplicate endpoints before the first hardware call. */
static bool valid_reads (const disci2c_read_t * p_reads, size_t count)
{
    if ((NULL == p_reads) || (0U == count) || (count > UTHOME_DISC_READS))
    {
        return false;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        if ((p_reads[index].address < 8U) || (p_reads[index].address > 119U))
        {
            return false;
        }
        for (size_t prior = 0U; prior < index; ++prior)
        {
            if (p_reads[prior].address == p_reads[index].address)
            {
                return false;
            }
        }
    }
    return true;
}

/** @brief NACK is a result; timeout or bus failure stops further probing. */
static void observe (disci2c_t *             p_context,
                     uint64_t                deadline_ms,
                     disci2c_result_data_t * p_result)
{
    for (size_t index = 0U; index < p_result->count; ++index)
    {
        uint64_t now_ms = 0U;
        if ((0 != p_context->p_now(&now_ms)) ||
            (now_ms < p_result->started_ms) || (now_ms >= deadline_ms))
        {
            p_result->status = DISCI2C_INCOMPLETE;
            break;
        }
        const uint64_t remaining_ms = deadline_ms - now_ms;
        const uint64_t end_ms =
            now_ms + ((remaining_ms < 100U) ? remaining_ms : 100U);
        disci2c_record_t * p_record = &p_result->records[index];
        p_record->status = read_byte(p_context->p_bus, end_ms, p_record);
        if ((DISCI2C_RESPONDING != p_record->status) &&
            (DISCI2C_NO_RESPONSE != p_record->status))
        {
            p_result->status = DISCI2C_INCOMPLETE;
            break;
        }
    }
}

/** @brief Do not expose a partial or late read as valid identity evidence. */
static disci2c_observation_t
read_byte (i2cbus_t * p_bus, uint64_t deadline_ms, disci2c_record_t * p_record)
{
    uint8_t                value   = 0U;
    const i2cbus_request_t request = {.address     = p_record->read.address,
                                      .p_write     = &p_record->read.reg,
                                      .write_size  = 1U,
                                      .p_read      = &value,
                                      .read_size   = 1U,
                                      .deadline_ms = deadline_ms};
    const int              result  = i2cbus_transfer(p_bus, &request);
    uint64_t               now_ms  = 0U;
    if (0 != i2cbus_now(p_bus, &now_ms))
    {
        return DISCI2C_IO_ERROR;
    }
    if ((I2CBUS_TIMEOUT == result) || (now_ms >= deadline_ms))
    {
        return DISCI2C_TIMED_OUT;
    }
    if (I2CBUS_NACK == result)
    {
        return DISCI2C_NO_RESPONSE;
    }
    if (0 != result)
    {
        return DISCI2C_IO_ERROR;
    }
    p_record->value = value;
    return DISCI2C_RESPONDING;
}

/* End of file. */

