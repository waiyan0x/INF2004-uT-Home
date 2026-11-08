/**
 * @file txrule.c
 * @brief Validate and atomically replace one bounded transaction rule.
 */
#include "txrule.h"
#include <string.h>

static bool name_valid (const char * p_name);

/** @brief Initialize caller-owned state before its first use. */
void txrule_init (txrule_t * p_state)
{
    if (NULL != p_state)
    {
        *p_state = (txrule_t){0};
    }
}

/** @brief Commit only a completely valid candidate; no I/O occurs here. */
txrule_status_t txrule_load (txrule_t *              p_state,
                             const txrule_config_t * p_candidate)
{
    if ((NULL == p_state) || (NULL == p_candidate) ||
        (1U != p_candidate->version) || (0U == p_candidate->revision) ||
        (!name_valid(p_candidate->device)) || (8U > p_candidate->address) ||
        (119U < p_candidate->address) || (255U < p_candidate->reg) ||
        (0U == p_candidate->read_size) ||
        (TXRULE_DATA_SIZE < p_candidate->read_size))
    {
        return TXRULE_INVALID;
    }
    p_state->active   = *p_candidate;
    p_state->b_loaded = true;
    return TXRULE_OK;
}

/** @brief Select the active transaction and expose bytes only on success. */
void txrule_execute (const txrule_t *  p_state,
                     const char *      p_device,
                     uint32_t          revision,
                     txrule_read_t *   p_read,
                     void *            p_context,
                     txrule_result_t * p_result)
{
    uint8_t data[TXRULE_DATA_SIZE] = {0U};
    if (NULL == p_result)
    {
        return;
    }
    *p_result = (txrule_result_t){.status = TXRULE_INVALID};
    if ((NULL == p_state) || (NULL == p_device) || (NULL == p_read))
    {
        return;
    }
    if (!p_state->b_loaded)
    {
        p_result->status = TXRULE_UNCONFIGURED;
        return;
    }
    p_result->rule = p_state->active;
    if ((revision != p_state->active.revision) ||
        (0 != strncmp(p_device, p_state->active.device, TXRULE_NAME_SIZE)))
    {
        p_result->status = TXRULE_MISMATCH;
        return;
    }
    p_result->status = p_read(p_context, &p_result->rule, data);
    if (TXRULE_OK == p_result->status)
    {
        /* The active rule was admitted by txrule_load; storage is owner-only.
         */
        p_result->size = (uint8_t)p_result->rule.read_size;
        memcpy(p_result->data, data, p_result->size);
    }
    else if (TXRULE_TIMEOUT != p_result->status)
    {
        p_result->status = TXRULE_IO;
    }
    else
    {
        /* Preserve the timeout with an empty result. */
    }
}

/** @brief Require ASCII identifiers with zero-filled bytes after the NUL. */
static bool name_valid (const char * p_name)
{
    bool b_end = false;
    if ('\0' == p_name[0])
    {
        return false;
    }
    for (size_t idx = 0U; idx < TXRULE_NAME_SIZE; ++idx)
    {
        const char value    = p_name[idx];
        const bool b_letter = (('a' <= value) && ('z' >= value)) ||
                              (('A' <= value) && ('Z' >= value));
        const bool b_digit = ('0' <= value) && ('9' >= value);
        if ('\0' == value)
        {
            b_end = true;
        }
        else if (b_end || ((!b_letter) && (!b_digit) && ('_' != value) &&
                           ('-' != value)))
        {
            return false;
        }
        else
        {
            /* This identifier byte is valid. */
        }
    }
    return b_end;
}

/* End of file. */

