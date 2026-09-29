/**
 * @file spifake.c
 * @brief Retain transaction boundaries, auxiliary b_levels and finite fake
 * time.
 */
#include "spifake.h"
#include <string.h>
static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach);
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms);
static const spibus_ops_t g_operations = {.p_attach   = attach,
                                          .p_transfer = transfer,
                                          .p_now      = now_ms,
                                          .p_delay    = delay_ms};

/** @brief Initialize a fresh fake only after its preceding devices are gone. */
void spifake_init (spifake_t * p_fake)
{
    if (NULL != p_fake)
    {
        memset(p_fake, 0, sizeof(*p_fake));
        p_fake->bus =
            (spibus_t){.p_operations = &g_operations, .p_context = p_fake};
        p_fake->transfer_ms = 1U;
        p_fake->error       = SPIBUS_IO;
    }
}

/** @brief Record output transitions and optionally reject a write. */
int spifake_set (void * p_context, bool b_high)
{
    spifake_line_t * p_line = p_context;
    if ((NULL == p_line) || (p_line->b_fail))
    {
        return -1;
    }
    p_line->b_levels[p_line->calls % 16U] = b_high;
    ++p_line->calls;
    p_line->b_high = b_high;
    return 0;
}

/** @brief Keep failures distinguishable from duplicate-CS front-end checks. */
static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach)
{
    spifake_t * p_fake = p_context;
    if (NULL == p_config)
    {
        return SPIBUS_INVALID;
    }
    return (b_attach ? p_fake->b_attach_fail : p_fake->b_detach_fail)
               ? SPIBUS_IO
               : 0;
}

/** @brief Capture one CS window, always deselecting on transport failure. */
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request)
{
    spifake_t *        p_fake = p_context;
    spifake_record_t * p_record =
        &p_fake->records[p_fake->transfers % UTHOME_SPI_LOG];
    *p_record =
        (spifake_record_t){.config = *p_config, .size = p_request->size};
    if (NULL != p_fake->p_data_lines[p_config->cs_gpio])
    {
        p_record->b_data = p_fake->p_data_lines[p_config->cs_gpio]->b_high;
    }
    if (NULL != p_request->p_write)
    {
        memcpy(p_record->bytes, p_request->p_write, p_request->size);
    }
    ++p_fake->transfers;
    p_fake->b_selected = true;
    int result         = 0;
    if ((p_fake->now_ms >= p_request->deadline_ms) ||
        (p_fake->transfer_ms >= (p_request->deadline_ms - p_fake->now_ms)))
    {
        p_fake->now_ms = p_request->deadline_ms;
        result         = SPIBUS_TIMEOUT;
    }
    else
    {
        p_fake->now_ms += p_fake->transfer_ms;
        if (p_fake->transfers == p_fake->fail_at)
        {
            result = p_fake->error;
        }
        else if (NULL != p_request->p_read)
        {
            if (NULL == p_request->p_write)
            {
                memset(p_request->p_read, 0xA5, p_request->size);
            }
            else
            {
                memmove(p_request->p_read, p_request->p_write, p_request->size);
            }
        }
        else
        {
            /* Write-only call deliberately discards input. */
        }
    }
    p_fake->b_selected = false;
    return result;
}

/** @brief Return the fake clock shared by transport and required delays. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    const spifake_t * p_fake = p_context;
    *p_now                   = p_fake->now_ms;
    return 0;
}

/** @brief Advance required time without manufacturing a new timeout budget. */
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms)
{
    spifake_t * p_fake = p_context;
    if ((p_fake->now_ms >= deadline_ms) ||
        (duration_ms >= (deadline_ms - p_fake->now_ms)))
    {
        return SPIBUS_TIMEOUT;
    }
    p_fake->now_ms += duration_ms;
    p_fake->delayed_ms += duration_ms;
    return 0;
}

/* End of file. */

