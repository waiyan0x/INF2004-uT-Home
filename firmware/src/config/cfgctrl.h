/**
 * @file cfgctrl.h
 * @brief Sequence independent validation, activation and durable saving.
 */
#ifndef UTHOME_CFGCTRL_H
#define UTHOME_CFGCTRL_H
#include "cfgwire.h"
#include "cfgstore.h"
typedef int
cfgctrl_validate_t (void * p_context, const uint8_t * p_data, size_t size);
typedef int cfgctrl_activate_t (void * p_context);
typedef int cfgctrl_recover_t (void * p_context);

typedef struct
{
    cfgctrl_validate_t * p_validate;
    cfgctrl_activate_t * p_activate;
    cfgctrl_recover_t *  p_recover;
} cfgctrl_ops_t;

typedef enum
{
    CFGCTRL_OK       = 0,
    CFGCTRL_SAVED    = 1,
    CFGCTRL_UNSAVED  = 2,
    CFGCTRL_REJECTED = 3,
    CFGCTRL_RECOVERY = 4,
    CFGCTRL_STALE    = 5,
    CFGCTRL_CONFLICT = 6,
    CFGCTRL_STORAGE  = 7,
    CFGCTRL_EMPTY    = 8
} cfgctrl_status_t;

#define UTHOME_CFG_RECEIVED      1U
#define UTHOME_CFG_VALIDATED     2U
#define UTHOME_CFG_ACTIVATED     4U
#define UTHOME_CFG_SAVED         8U
#define UTHOME_CFG_DURABLE_KNOWN 16U
#define UTHOME_CFG_RECOVERY      32U
#define UTHOME_CFG_ACTIVE_KNOWN  64U

typedef struct
{
    const cfgctrl_ops_t * p_operations;
    void *                p_context;
    cfgstore_t *          p_store;
    uint8_t               active[UTHOME_CONFIG_BYTES];
    uint8_t               cached[UTHOME_CONFIG_BYTES];
    size_t                active_size;
    size_t                cached_size;
    uint32_t              active_revision;
    uint32_t              active_crc;
    uint32_t              saved_revision;
    uint32_t              saved_crc;
    uint32_t              last_id;
    uint32_t              last_revision;
    cfgwire_command_t     last_command;
    uint32_t              reply[6];
    bool                  b_restore_attempted;
    bool                  b_initialized;
    bool                  b_busy;
    bool                  b_recovery;
    bool                  b_saved_known;
} cfgctrl_t;

/* Zero-initialize static/caller storage; never place this context on a small
 * worker stack. Single controller task, no concurrent calls or callback
 * reentry. Init borrows dependencies and performs no I/O. No callback defaults
 * to accept. validate must parse/admit the complete node, build a candidate in
 * owned storage without I/O, and return zero only on success. activate then
 * consumes that candidate: 0=active, 1=rejected/prior state restored,
 * other=recovery. recover returns zero only after all old resources are
 * released and empty. restore may run once before requests/activation. A stored
 * CRC is not admission. IDs strictly increase per boot; the latest identical
 * request gets its exact cached reply. Older IDs fail. Revisions never wrap;
 * compare payloads exactly. Successful handle returns a reply, not necessarily
 * successful deployment. PROBE bypasses sequence checks, does no I/O and
 * never evicts the retry cache. Its first reply field is last_id, not status.
 * Request bytes must remain stable and may not alias this context/store
 * scratch. */
int cfgctrl_init (cfgctrl_t *           p_control,
                  cfgstore_t *          p_store,
                  const cfgctrl_ops_t * p_operations,
                  void *                p_context);
cfgctrl_status_t cfgctrl_restore (cfgctrl_t * p_control);
int cfgctrl_handle (cfgctrl_t *               p_control,
                    const cfgwire_request_t * p_request,
                    uint32_t *                p_reply);
#endif
/* End of file. */

