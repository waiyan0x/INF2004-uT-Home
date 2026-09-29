/**
 * @file cfgctrl.c
 * @brief Keep active and saved revisions distinct across retries and failures.
 */
#include "cfgctrl.h"
#include <string.h>
static int refresh_saved (cfgctrl_t * p_control, cfgstore_record_t * p_record);
static cfgctrl_status_t apply_request (cfgctrl_t *               p_control,
                                       const cfgwire_request_t * p_request,
                                       uint32_t *                p_flags);
static cfgctrl_status_t activate_candidate (cfgctrl_t *     p_control,
                                            const uint8_t * p_data,
                                            size_t          size,
                                            uint32_t        revision,
                                            uint32_t *      p_flags);
static cfgctrl_status_t save_active (cfgctrl_t * p_control, uint32_t * p_flags);
static cfgctrl_status_t dispatch (cfgctrl_t *               p_control,
                                  const cfgwire_request_t * p_request,
                                  uint32_t *                p_flags);
static bool valid_request (const cfgwire_request_t * p_request);
static void make_reply (const cfgctrl_t * p_control,
                        cfgctrl_status_t  status,
                        uint32_t          flags,
                        uint32_t *        p_reply);

/** @brief Require every boundary implementation, never infer target support. */
int cfgctrl_init (cfgctrl_t *           p_control,
                  cfgstore_t *          p_store,
                  const cfgctrl_ops_t * p_operations,
                  void *                p_context)
{
    if ((NULL == p_control) || (NULL == p_store) || (NULL == p_operations) ||
        (NULL == p_operations->p_validate) ||
        (NULL == p_operations->p_activate) ||
        (NULL == p_operations->p_recover) || (p_control->b_initialized))
    {
        return -1;
    }
    p_control->p_store       = p_store;
    p_control->p_operations  = p_operations;
    p_control->p_context     = p_context;
    p_control->b_initialized = true;
    return 0;
}

/** @brief Revalidate stored bytes against this firmware before boot activation.
 */
cfgctrl_status_t cfgctrl_restore (cfgctrl_t * p_control)
{
    if ((NULL == p_control) || (!p_control->b_initialized) ||
        (p_control->b_busy) || (0U != p_control->last_id) ||
        (0U != p_control->active_revision) || (p_control->b_recovery) ||
        (p_control->b_restore_attempted))
    {
        return CFGCTRL_REJECTED;
    }
    p_control->b_busy              = true;
    p_control->b_restore_attempted = true;
    cfgstore_record_t record       = {0};
    const int         loaded       = refresh_saved(p_control, &record);
    cfgctrl_status_t  status       = CFGCTRL_STORAGE;
    if (CFGSTORE_EMPTY == loaded)
    {
        status = CFGCTRL_EMPTY;
    }
    else if (CFGSTORE_OK == loaded)
    {
        uint32_t flags = 0U;
        status         = activate_candidate(
            p_control, p_control->cached, record.size, record.revision, &flags);
        if (CFGCTRL_OK == status)
        {
            status = CFGCTRL_SAVED;
        }
    }
    else
    {
        /* Storage fault is visible; a later query can retry the read. */
    }
    p_control->b_busy = false;
    return status;
}

/** @brief Cache one exact request, including rejected and uncertain-save
 * replies. */
int cfgctrl_handle (cfgctrl_t *               p_control,
                    const cfgwire_request_t * p_request,
                    uint32_t *                p_reply)
{
    if ((NULL == p_control) || (!p_control->b_initialized) ||
        (p_control->b_busy) || (NULL == p_reply) || (!valid_request(p_request)))
    {
        return -1;
    }
    if (CFGWIRE_PROBE == p_request->command)
    {
        /* Read-only discovery cannot evict a retry or consume a request ID.
         * Field zero is last_id for PROBE only; the remaining fields retain
         * their meanings. Durability is the last known observation, no I/O. */
        make_reply(p_control, CFGCTRL_OK, UTHOME_CFG_RECEIVED, p_reply);
        p_reply[0] = p_control->last_id;
        return 0;
    }
    if (p_request->request_id == p_control->last_id)
    {
        if ((p_request->revision != p_control->last_revision) ||
            (p_request->command != p_control->last_command) ||
            (p_request->size != p_control->cached_size) ||
            ((0U != p_request->size) &&
             (0 !=
              memcmp(p_request->p_data, p_control->cached, p_request->size))))
        {
            make_reply(
                p_control, CFGCTRL_CONFLICT, UTHOME_CFG_RECEIVED, p_reply);
        }
        else
        {
            memcpy(p_reply, p_control->reply, sizeof(p_control->reply));
        }
        return 0;
    }
    if (p_request->request_id < p_control->last_id)
    {
        make_reply(p_control, CFGCTRL_STALE, UTHOME_CFG_RECEIVED, p_reply);
        return 0;
    }
    p_control->b_busy             = true;
    uint32_t               flags  = UTHOME_CFG_RECEIVED;
    const cfgctrl_status_t status = dispatch(p_control, p_request, &flags);
    make_reply(p_control, status, flags, p_control->reply);
    p_control->last_id       = p_request->request_id;
    p_control->last_revision = p_request->revision;
    p_control->last_command  = p_request->command;
    p_control->cached_size   = p_request->size;
    if (0U != p_request->size)
    {
        memcpy(p_control->cached, p_request->p_data, p_request->size);
    }
    memcpy(p_reply, p_control->reply, sizeof(p_control->reply));
    p_control->b_busy = false;
    return 0;
}

/** @brief Unknown durability is explicit until a synchronized load succeeds. */
static int refresh_saved (cfgctrl_t * p_control, cfgstore_record_t * p_record)
{
    p_control->b_saved_known  = false;
    p_control->saved_revision = 0U;
    p_control->saved_crc      = 0U;
    const int result          = cfgstore_load(p_control->p_store,
                                     p_control->cached,
                                     sizeof(p_control->cached),
                                     p_record);
    if ((CFGSTORE_OK == result) || (CFGSTORE_EMPTY == result))
    {
        p_control->b_saved_known  = true;
        p_control->saved_revision = p_record->revision;
        p_control->saved_crc      = p_record->crc;
    }
    return result;
}

/** @brief Validation must complete before activation or durable writes begin.
 */
static cfgctrl_status_t apply_request (cfgctrl_t *               p_control,
                                       const cfgwire_request_t * p_request,
                                       uint32_t *                p_flags)
{
    cfgstore_record_t saved  = {0};
    const int         loaded = refresh_saved(p_control, &saved);
    if ((CFGSTORE_OK != loaded) && (CFGSTORE_EMPTY != loaded))
    {
        return CFGCTRL_STORAGE;
    }
    if ((p_request->revision < p_control->active_revision) ||
        (p_request->revision < saved.revision))
    {
        return CFGCTRL_STALE;
    }
    if ((p_request->revision == saved.revision) &&
        ((p_request->size != saved.size) ||
         (0 != memcmp(p_request->p_data, p_control->cached, saved.size))))
    {
        return CFGCTRL_CONFLICT;
    }
    if (p_request->revision == p_control->active_revision)
    {
        if ((p_request->size != p_control->active_size) ||
            (0 != memcmp(p_request->p_data,
                         p_control->active,
                         p_control->active_size)))
        {
            return CFGCTRL_CONFLICT;
        }
        return save_active(p_control, p_flags);
    }
    const cfgctrl_status_t activated = activate_candidate(p_control,
                                                          p_request->p_data,
                                                          p_request->size,
                                                          p_request->revision,
                                                          p_flags);
    return (CFGCTRL_OK == activated) ? save_active(p_control, p_flags)
                                     : activated;
}

/** @brief An unclassified activation failure conservatively enters recovery. */
static cfgctrl_status_t activate_candidate (cfgctrl_t *     p_control,
                                            const uint8_t * p_data,
                                            size_t          size,
                                            uint32_t        revision,
                                            uint32_t *      p_flags)
{
    if (0 !=
        p_control->p_operations->p_validate(p_control->p_context, p_data, size))
    {
        return CFGCTRL_REJECTED;
    }
    *p_flags |= UTHOME_CFG_VALIDATED;
    const int result =
        p_control->p_operations->p_activate(p_control->p_context);
    if (1 == result)
    {
        return CFGCTRL_REJECTED;
    }
    if (0 != result)
    {
        p_control->b_recovery      = true;
        p_control->active_revision = 0U;
        p_control->active_crc      = 0U;
        p_control->active_size     = 0U;
        return CFGCTRL_RECOVERY;
    }
    memcpy(p_control->active, p_data, size);
    p_control->active_size     = size;
    p_control->active_revision = revision;
    p_control->active_crc      = ~cfgbytes_crc(UINT32_MAX, p_data, size);
    *p_flags |= UTHOME_CFG_ACTIVATED;
    return CFGCTRL_OK;
}

/** @brief A save failure never reports that active RAM state was rolled back.
 */
static cfgctrl_status_t save_active (cfgctrl_t * p_control, uint32_t * p_flags)
{
    const int saved = cfgstore_save(p_control->p_store,
                                    p_control->active,
                                    p_control->active_size,
                                    p_control->active_revision);
    if (CFGSTORE_OK != saved)
    {
        p_control->b_saved_known  = false;
        p_control->saved_revision = 0U;
        p_control->saved_crc      = 0U;
        return CFGCTRL_UNSAVED;
    }
    p_control->b_saved_known  = true;
    p_control->saved_revision = p_control->active_revision;
    p_control->saved_crc      = p_control->active_crc;
    *p_flags |= UTHOME_CFG_SAVED;
    return CFGCTRL_SAVED;
}

/** @brief Queries remain available while activation requires explicit recovery.
 */
static cfgctrl_status_t dispatch (cfgctrl_t *               p_control,
                                  const cfgwire_request_t * p_request,
                                  uint32_t *                p_flags)
{
    if (CFGWIRE_QUERY == p_request->command)
    {
        cfgstore_record_t record = {0};
        const int         loaded = refresh_saved(p_control, &record);
        if ((CFGSTORE_OK != loaded) && (CFGSTORE_EMPTY != loaded))
        {
            return CFGCTRL_STORAGE;
        }
        return p_control->b_recovery ? CFGCTRL_RECOVERY : CFGCTRL_OK;
    }
    if (CFGWIRE_RECOVER == p_request->command)
    {
        if (!p_control->b_recovery)
        {
            return CFGCTRL_REJECTED;
        }
        if (0 != p_control->p_operations->p_recover(p_control->p_context))
        {
            return CFGCTRL_RECOVERY;
        }
        p_control->b_recovery = false;
        return CFGCTRL_EMPTY;
    }
    if (p_control->b_recovery)
    {
        return CFGCTRL_RECOVERY;
    }
    if (CFGWIRE_SAVE == p_request->command)
    {
        if ((0U == p_control->active_revision) ||
            (p_request->revision != p_control->active_revision))
        {
            return CFGCTRL_REJECTED;
        }
        return save_active(p_control, p_flags);
    }
    return apply_request(p_control, p_request, p_flags);
}

/** @brief Direct callers must obey the same request bounds as the wire decoder.
 */
static bool valid_request (const cfgwire_request_t * p_request)
{
    if ((NULL == p_request) || (0U == p_request->request_id) ||
        (p_request->command < CFGWIRE_APPLY) ||
        (p_request->command > CFGWIRE_PROBE) ||
        (p_request->size > UTHOME_CONFIG_BYTES))
    {
        return false;
    }
    if (CFGWIRE_APPLY == p_request->command)
    {
        return (NULL != p_request->p_data) && (0U != p_request->size) &&
               (0U != p_request->revision);
    }
    return (0U == p_request->size) &&
           ((CFGWIRE_SAVE == p_request->command) ? (0U != p_request->revision)
                                                 : (0U == p_request->revision));
}

/** @brief Flags describe reached milestones plus current known state. */
static void make_reply (const cfgctrl_t * p_control,
                        cfgctrl_status_t  status,
                        uint32_t          flags,
                        uint32_t *        p_reply)
{
    /* Public statuses are the nonnegative wire values 0..8. */
    p_reply[0] = (uint32_t)status;
    p_reply[1] = p_control->active_revision;
    p_reply[2] = p_control->saved_revision;
    p_reply[3] = p_control->active_crc;
    p_reply[4] = p_control->saved_crc;
    p_reply[5] =
        flags | (p_control->b_saved_known ? UTHOME_CFG_DURABLE_KNOWN : 0U) |
        (p_control->b_recovery ? UTHOME_CFG_RECOVERY : 0U) |
        ((0U != p_control->active_revision) ? UTHOME_CFG_ACTIVE_KNOWN : 0U);
}

/* End of file. */

