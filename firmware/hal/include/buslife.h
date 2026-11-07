/**
 * @file buslife.h
 * @brief Shared permanent-lock and repeatable-session lifecycle.
 */
#ifndef UTHOME_BUSLIFE_H
#define UTHOME_BUSLIFE_H

#include <stdbool.h>
#include <stdint.h>

/* Permanent lifecycle. Only buslife_init leaves UNINIT, exactly once. */
typedef enum
{
    BUSLIFE_UNINIT = 0,
    BUSLIFE_READY  = 1,
    BUSLIFE_FAILED = 2
} buslife_status_t;

/* Session phase. RETAINED means cleanup failed: retry close, never reuse. */
typedef enum
{
    BUSLIFE_CLOSED   = 0,
    BUSLIFE_OPEN     = 1,
    BUSLIFE_RETAINED = 2
} buslife_phase_t;

/* Create the permanent lock; called at most once, during buslife_init. */
typedef int buslife_create_t (void * p_context);
/* Report whether the caller is the serialized lifecycle owner. */
typedef bool buslife_owner_t (void * p_context);
/* Lock within timeout_ms; zero polls. Return 0, the adapter timeout code or a
 * failure.
 */
typedef int buslife_lock_t (void * p_context, uint32_t timeout_ms);
typedef int buslife_unlock_t (void * p_context);
/* Acquire pins and start the controller. Failure must leave neither held. */
typedef int buslife_start_t (void * p_context, const void * p_settings);
/* Stop the controller and release pins. Failure keeps pin ownership. */
typedef int buslife_stop_t (void * p_context);

typedef struct
{
    buslife_create_t * p_create;
    buslife_owner_t *  p_owner;
    buslife_lock_t *   p_lock;
    buslife_unlock_t * p_unlock;
    buslife_start_t *  p_start;
    buslife_stop_t *   p_stop;
} buslife_ops_t;

/* Zero-initialize permanent storage. Phase changes happen with the lock held.
 * Operations and context remain alive for the life of the program.
 */
typedef struct
{
    const buslife_ops_t * p_operations;
    void *                p_context;
    buslife_status_t      status;
    buslife_phase_t       phase;
} buslife_t;

/* Immutable adapter result codes; callbacks return that same result domain.
 * Every operation receives a non-null table owned by its protocol wrapper.
 */
typedef struct
{
    int invalid;
    int state;
    int io_error;
    int timeout;
} buslife_errors_t;

/* Startup only; repeated success/failure never creates a second lock. */
int buslife_init (buslife_t *              p_owner,
                  const buslife_ops_t *    p_operations,
                  void *                   p_context,
                  const buslife_errors_t * p_errors);
/* Owner-only phase checks precede wrapper-specific claims/device inspection. */
bool buslife_can_open (const buslife_t * p_owner);
bool buslife_can_close (const buslife_t * p_owner);
/* The serialized owner must pass can_open/can_close and its protocol checks
 * immediately before start/stop in the same serialized operation. Settings
 * live through the synchronous start call; workers never change the phase.
 */
int buslife_start (buslife_t *              p_owner,
                   const void *             p_settings,
                   const buslife_errors_t * p_errors);
int buslife_stop (buslife_t * p_owner, const buslife_errors_t * p_errors);
/* Workers acquire a finite transfer lock, then recheck the open phase. */
int buslife_begin (buslife_t *              p_owner,
                   uint32_t                 timeout_ms,
                   const buslife_errors_t * p_errors);
int buslife_end (buslife_t * p_owner, const buslife_errors_t * p_errors);
/* A hint only; begin performs the authoritative locked phase check. */
bool buslife_is_open (const buslife_t * p_owner);

#endif

/* End of file. */

