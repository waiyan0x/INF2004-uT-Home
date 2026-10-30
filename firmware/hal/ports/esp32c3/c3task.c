/**
 * @file c3task.c
 * @brief Fixed-resource wireless service-task management and observation.
 */

#include "c3task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(UTHOME_C3TASK_HOST_TEST) && UTHOME_C3TASK_HOST_TEST
#define C3TASK_HOST_BUILD 1
#elif defined(UTHOME_C3TASK_TARGET) && UTHOME_C3TASK_TARGET
#define C3TASK_HOST_BUILD 0
#include <tk/tkernel.h>
#else
#define C3TASK_HOST_BUILD 1
#endif

/*
 * Task resources have not yet been approved or allocated.
 * The public interface must not report successful initialization.
 *
 * No heap allocation or native kernel object creation occurs here.
 */

int c3task_init (void)
{
    return -1;
}

uint32_t c3task_capacity (void)
{
    return 0U;
}

uint32_t c3task_used (void)
{
    return 0U;
}

/*
 * Preliminary reservation helpers retained for existing host tests.
 * They are not yet part of the target runtime.
 */

#if defined(UTHOME_C3TASK_HOST_TEST) && UTHOME_C3TASK_HOST_TEST

typedef enum
{
    C3TASK_SLOT_FREE = 0,
    C3TASK_SLOT_RESERVED,
    C3TASK_SLOT_RUNNING,
    C3TASK_SLOT_STOPPING
} c3task_slot_state_t;

typedef struct
{
    c3task_slot_state_t state;
    int32_t             kernel_task_id;
    int32_t             notification_sem_id;
    uint32_t            stack_capacity_bytes;
    uint32_t            requested_stack_bytes;
} c3task_slot_t;

static bool c3task_slot_available (const c3task_slot_t * p_slot)
{
    return (NULL != p_slot) && (C3TASK_SLOT_FREE == p_slot->state);
}

static bool c3task_slot_reserve (c3task_slot_t * p_slot,
                                 uint32_t        requested_stack_bytes)
{
    if ((!c3task_slot_available(p_slot)) || (0U == requested_stack_bytes) ||
        (requested_stack_bytes > p_slot->stack_capacity_bytes))
    {
        return false;
    }

    p_slot->state                 = C3TASK_SLOT_RESERVED;
    p_slot->requested_stack_bytes = requested_stack_bytes;

    return true;
}

static int32_t c3task_find_and_reserve (c3task_slot_t * p_slots,
                                        uint32_t        slot_count,
                                        uint32_t        requested_stack_bytes)
{
    if ((NULL == p_slots) || (0U == slot_count) ||
        (0U == requested_stack_bytes))
    {
        return -1;
    }

    for (uint32_t index = 0U; index < slot_count; ++index)
    {
        if (c3task_slot_reserve(&p_slots[index], requested_stack_bytes))
        {
            return (int32_t)index;
        }
    }

    return -1;
}

#endif /* UTHOME_C3TASK_HOST_TEST */

/* -------------------------------------------------------------------------
 * Fixed-capacity wireless task observer
 * ------------------------------------------------------------------------- */

/*
 * Observer records are statically allocated.
 *
 * ESP32-C3 updates use interrupt-mask protection.
 * Mac host tests use no-op protection and must be single-threaded.
 *
 * Observer slots are diagnostic records, not task stack allocations.
 */

typedef struct
{
    c3task_observer_record_t record;
    uint32_t                 generation;
} c3task_observer_slot_t;

static c3task_observer_stats_t g_observer_stats;
static c3task_observer_slot_t  g_observer_slots[C3TASK_OBSERVER_SLOTS];

/**
 * @brief Enter the observer critical section.
 */
static uint32_t c3task_observer_enter (void)
{
#if C3TASK_HOST_BUILD
    return 0U;
#else
    return (uint32_t)disint();
#endif
}

/**
 * @brief Restore the previous interrupt state.
 */
static void c3task_observer_leave (uint32_t saved)
{
#if C3TASK_HOST_BUILD
    (void)saved;
#else
    enaint((UW)saved);
#endif
}

/**
 * @brief Copy a bounded task name without dynamic allocation.
 */
static void c3task_copy_name (char * p_destination, const char * p_name)
{
    uint32_t index = 0U;

    if (NULL != p_name)
    {
        while ((index < (C3TASK_OBSERVER_NAME_BYTES - 1U)) &&
               ('\0' != p_name[index]))
        {
            p_destination[index] = p_name[index];
            ++index;
        }
    }

    p_destination[index] = '\0';
}

/**
 * @brief Find a pending record using its observation token.
 *
 * Must be called while holding the observer critical section.
 */
static int32_t c3task_pending_index (uint32_t token)
{
    if (0U == token)
    {
        return -1;
    }

    for (uint32_t index = 0U; index < C3TASK_OBSERVER_SLOTS; ++index)
    {
        if ((C3TASK_OBSERVER_PENDING == g_observer_slots[index].record.state) &&
            (token == g_observer_slots[index].generation))
        {
            return (int32_t)index;
        }
    }

    return -1;
}

/**
 * @brief Reset task-observation statistics and records.
 *
 * Call only before observed task activity begins.
 */
void c3task_observer_reset (void)
{
    const uint32_t saved = c3task_observer_enter();

    memset(&g_observer_stats, 0, sizeof(g_observer_stats));
    memset(&g_observer_slots, 0, sizeof(g_observer_slots));

    c3task_observer_leave(saved);
}

/**
 * @brief Begin observing one task-creation request.
 */
uint32_t c3task_observer_begin (const char * p_name, uint32_t stack_bytes)
{
    uint32_t       token = 0U;
    const uint32_t saved = c3task_observer_enter();

    ++g_observer_stats.requests;

    if (stack_bytes > g_observer_stats.maximum_requested_stack)
    {
        g_observer_stats.maximum_requested_stack = stack_bytes;
    }

    for (uint32_t index = 0U; index < C3TASK_OBSERVER_SLOTS; ++index)
    {
        c3task_observer_slot_t * p_slot = &g_observer_slots[index];

        if (C3TASK_OBSERVER_FREE == p_slot->record.state)
        {
            /*
             * Encode the slot index and generation in one token.
             * A recycled slot receives a different token.
             */
            uint32_t generation =
                (p_slot->generation / C3TASK_OBSERVER_SLOTS) + 1U;

            if (generation >
                ((UINT32_MAX - C3TASK_OBSERVER_SLOTS) / C3TASK_OBSERVER_SLOTS))
            {
                generation = 1U;
            }

            token = (generation * C3TASK_OBSERVER_SLOTS) + index + 1U;

            p_slot->generation                   = token;
            p_slot->record.handle                = (uintptr_t)0U;
            p_slot->record.requested_stack_bytes = stack_bytes;
            p_slot->record.state                 = C3TASK_OBSERVER_PENDING;

            c3task_copy_name(p_slot->record.name, p_name);
            break;
        }
    }

    if (0U == token)
    {
        ++g_observer_stats.record_overflows;
    }

    c3task_observer_leave(saved);
    return token;
}

/**
 * @brief Commit a successful task creation.
 */
int c3task_observer_commit (uint32_t token, void * p_handle)
{
    int            result = -1;
    const uint32_t saved  = c3task_observer_enter();
    const int32_t  index  = c3task_pending_index(token);

    if ((0 <= index) && (NULL != p_handle))
    {
        bool b_duplicate = false;

        for (uint32_t item = 0U; item < C3TASK_OBSERVER_SLOTS; ++item)
        {
            if ((C3TASK_OBSERVER_ACTIVE ==
                 g_observer_slots[item].record.state) &&
                ((uintptr_t)p_handle == g_observer_slots[item].record.handle))
            {
                b_duplicate = true;
                break;
            }
        }

        if (!b_duplicate)
        {
            c3task_observer_record_t * p_record =
                &g_observer_slots[(uint32_t)index].record;

            p_record->handle = (uintptr_t)p_handle;
            p_record->state  = C3TASK_OBSERVER_ACTIVE;

            ++g_observer_stats.successes;
            ++g_observer_stats.active;

            if (g_observer_stats.active > g_observer_stats.peak_active)
            {
                g_observer_stats.peak_active = g_observer_stats.active;
            }

            result = 0;
        }
    }

    c3task_observer_leave(saved);
    return result;
}

/**
 * @brief Abort a task request after creation failure.
 */
int c3task_observer_abort (uint32_t token)
{
    int            result = -1;
    const uint32_t saved  = c3task_observer_enter();
    const int32_t  index  = c3task_pending_index(token);

    if (0 <= index)
    {
        c3task_observer_record_t * p_record =
            &g_observer_slots[(uint32_t)index].record;

        memset(p_record, 0, sizeof(*p_record));

        ++g_observer_stats.failures;
        result = 0;
    }

    c3task_observer_leave(saved);
    return result;
}

/**
 * @brief Cancel a committed task record after task startup fails.
 *
 * A cancelled task never began executing. Restore the successful-creation
 * counter and record the failed startup without counting a task deletion.
 */
int c3task_observer_cancel (void * p_handle)
{
    int            result = -1;
    const uint32_t saved  = c3task_observer_enter();

    if (NULL != p_handle)
    {
        for (uint32_t index = 0U; index < C3TASK_OBSERVER_SLOTS; ++index)
        {
            c3task_observer_record_t * p_record =
                &g_observer_slots[index].record;

            if ((C3TASK_OBSERVER_ACTIVE == p_record->state) &&
                ((uintptr_t)p_handle == p_record->handle))
            {
                memset(p_record, 0, sizeof(*p_record));

                if (0U < g_observer_stats.active)
                {
                    --g_observer_stats.active;
                }
                if (0U < g_observer_stats.successes)
                {
                    --g_observer_stats.successes;
                }
                ++g_observer_stats.failures;

                result = 0;
                break;
            }
        }
    }

    c3task_observer_leave(saved);
    return result;
}

/**
 * @brief Release one active task record by its handle.
 */
int c3task_observer_release (void * p_handle)
{
    int            result = -1;
    const uint32_t saved  = c3task_observer_enter();

    if (NULL != p_handle)
    {
        for (uint32_t index = 0U; index < C3TASK_OBSERVER_SLOTS; ++index)
        {
            c3task_observer_record_t * p_record =
                &g_observer_slots[index].record;

            if ((C3TASK_OBSERVER_ACTIVE == p_record->state) &&
                ((uintptr_t)p_handle == p_record->handle))
            {
                memset(p_record, 0, sizeof(*p_record));

                ++g_observer_stats.deletions;

                if (0U < g_observer_stats.active)
                {
                    --g_observer_stats.active;
                }

                result = 0;
                break;
            }
        }
    }

    c3task_observer_leave(saved);
    return result;
}

/**
 * @brief Obtain a consistent copy of an observer record.
 */
int c3task_observer_record (uint32_t index, c3task_observer_record_t * p_out)
{
    if ((C3TASK_OBSERVER_SLOTS <= index) || (NULL == p_out))
    {
        return -1;
    }

    const uint32_t saved = c3task_observer_enter();

    *p_out = g_observer_slots[index].record;

    c3task_observer_leave(saved);
    return 0;
}

/**
 * @brief Return a consistent snapshot of aggregate statistics.
 */
void c3task_observer_snapshot (c3task_observer_stats_t * p_out)
{
    if (NULL != p_out)
    {
        const uint32_t saved = c3task_observer_enter();

        *p_out = g_observer_stats;

        c3task_observer_leave(saved);
    }
}

/* -------------------------------------------------------------------------
 * Legacy counter-only observer interface
 * ------------------------------------------------------------------------- */

/*
 * These functions are retained for existing host tests.
 * Do not mix them with the handle-aware API in a runtime session.
 */

/**
 * @brief Record a legacy task-creation request.
 */
void c3task_observer_request (const char * p_name, uint32_t stack_bytes)
{
    (void)p_name;

    const uint32_t saved = c3task_observer_enter();

    ++g_observer_stats.requests;

    if (stack_bytes > g_observer_stats.maximum_requested_stack)
    {
        g_observer_stats.maximum_requested_stack = stack_bytes;
    }

    c3task_observer_leave(saved);
}

/**
 * @brief Record a legacy successful task creation.
 */
void c3task_observer_created (void)
{
    const uint32_t saved = c3task_observer_enter();

    ++g_observer_stats.successes;
    ++g_observer_stats.active;

    if (g_observer_stats.active > g_observer_stats.peak_active)
    {
        g_observer_stats.peak_active = g_observer_stats.active;
    }

    c3task_observer_leave(saved);
}

/**
 * @brief Record a legacy failed task creation.
 */
void c3task_observer_failed (void)
{
    const uint32_t saved = c3task_observer_enter();

    ++g_observer_stats.failures;

    c3task_observer_leave(saved);
}

/**
 * @brief Record a legacy task deletion.
 */
void c3task_observer_deleted (void)
{
    const uint32_t saved = c3task_observer_enter();

    ++g_observer_stats.deletions;

    if (0U < g_observer_stats.active)
    {
        --g_observer_stats.active;
    }

    c3task_observer_leave(saved);
}

/* End of file. */

