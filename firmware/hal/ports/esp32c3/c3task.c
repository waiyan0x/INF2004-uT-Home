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
 * The isolated Wi-Fi and BLE builds have different, mutually exclusive task
 * sets. Capacities are the actual byte requests made by their selected ESP-IDF
 * 6.1 paths. ESP-IDF already includes its libc allowance in those requests.
 * The pinned kernel has a separate 4 KiB interrupt stack, so no unmeasured
 * per-task interrupt allowance is added here.
 */

#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
#define C3TASK_STACK_SLOTS   4U
#define C3TASK_STACK_0_BYTES 2816U
#define C3TASK_STACK_1_BYTES 3584U
#define C3TASK_STACK_2_BYTES 4096U
#define C3TASK_STACK_3_BYTES 6656U
#elif defined(UTHOME_BLE_PROBE) && UTHOME_BLE_PROBE
#define C3TASK_STACK_SLOTS   3U
#define C3TASK_STACK_0_BYTES 4096U
#define C3TASK_STACK_1_BYTES 4096U
#define C3TASK_STACK_2_BYTES 4096U
#elif defined(UTHOME_C3TASK_HOST_TEST) && UTHOME_C3TASK_HOST_TEST
#define C3TASK_STACK_SLOTS   4U
#define C3TASK_STACK_0_BYTES 2816U
#define C3TASK_STACK_1_BYTES 3584U
#define C3TASK_STACK_2_BYTES 4096U
#define C3TASK_STACK_3_BYTES 6656U
#else
#define C3TASK_STACK_SLOTS 0U
#endif

#define C3TASK_STACK_ALIGNMENT    16U
#define C3TASK_REAPER_STACK_BYTES 1024U
#define C3TASK_REAPER_PRIORITY    31
#define C3TASK_REAPER_PERIOD_MS   100
#define C3TASK_ALIGNED_16         __attribute__((aligned(16)))

typedef struct
{
    uintptr_t owner;
    bool      b_used;
} c3task_stack_slot_t;

typedef enum
{
    C3TASK_REAPER_OFF = 0,
    C3TASK_REAPER_STARTING,
    C3TASK_REAPER_READY,
    C3TASK_REAPER_FAILED
} c3task_reaper_state_t;

#if 0U < C3TASK_STACK_SLOTS
static uint8_t g_stack_0[C3TASK_STACK_0_BYTES] C3TASK_ALIGNED_16;
static uint8_t g_stack_1[C3TASK_STACK_1_BYTES] C3TASK_ALIGNED_16;
static uint8_t g_stack_2[C3TASK_STACK_2_BYTES] C3TASK_ALIGNED_16;
#if 4U == C3TASK_STACK_SLOTS
static uint8_t g_stack_3[C3TASK_STACK_3_BYTES] C3TASK_ALIGNED_16;
#endif
#if !C3TASK_HOST_BUILD
static uint8_t g_reaper_stack[C3TASK_REAPER_STACK_BYTES] C3TASK_ALIGNED_16;
#endif
static c3task_stack_slot_t   g_stack_slots[C3TASK_STACK_SLOTS];
static c3task_reap_t *       gp_reap        = NULL;
static c3task_reaper_state_t g_reaper_state = C3TASK_REAPER_OFF;

static uint32_t c3task_resource_enter (void);
static void c3task_resource_leave (uint32_t saved);
static uint32_t c3task_stack_capacity_at (uint32_t index);
static void * c3task_stack_at (uint32_t index);
static int32_t c3task_stack_best_fit (uint32_t requested_bytes);
#if !C3TASK_HOST_BUILD
static void c3task_reaper_task (INT code, void * p_context);
#endif
#endif

/** @brief Enter the task-resource critical section. */
static uint32_t c3task_resource_enter (void)
{
#if C3TASK_HOST_BUILD
    return 0U;
#else
    return (uint32_t)disint();
#endif
}

/** @brief Restore the previous interrupt state. */
static void c3task_resource_leave (uint32_t saved)
{
#if C3TASK_HOST_BUILD
    (void)saved;
#else
    enaint((UW)saved);
#endif
}

/** @brief Start the bounded deferred-cleanup execution context. */
int c3task_init (void)
{
#if 0U == C3TASK_STACK_SLOTS
    return -1;
#else
    uint32_t saved = c3task_resource_enter();

    if (C3TASK_REAPER_READY == g_reaper_state)
    {
        c3task_resource_leave(saved);
        return 0;
    }
    if (C3TASK_REAPER_OFF != g_reaper_state)
    {
        c3task_resource_leave(saved);
        return -1;
    }
    g_reaper_state = C3TASK_REAPER_STARTING;
    c3task_resource_leave(saved);

#if C3TASK_HOST_BUILD
    saved          = c3task_resource_enter();
    g_reaper_state = C3TASK_REAPER_READY;
    c3task_resource_leave(saved);
    return 0;
#else
    if (0U != ((uintptr_t)g_reaper_stack & (C3TASK_STACK_ALIGNMENT - 1U)))
    {
        saved          = c3task_resource_enter();
        g_reaper_state = C3TASK_REAPER_FAILED;
        c3task_resource_leave(saved);
        return -1;
    }

    T_CTSK   config = {.exinf   = NULL,
                       .tskatr  = TA_HLNG | TA_RNG0 | TA_USERBUF,
                       .task    = (FP)c3task_reaper_task,
                       .itskpri = C3TASK_REAPER_PRIORITY,
                       .stksz   = (SZ)sizeof(g_reaper_stack),
                       .bufptr  = g_reaper_stack};
    const ID h_task = tk_cre_tsk(&config);

    if (h_task <= 0)
    {
        saved          = c3task_resource_enter();
        g_reaper_state = C3TASK_REAPER_FAILED;
        c3task_resource_leave(saved);
        return -1;
    }
    if (E_OK != tk_sta_tsk(h_task, 0))
    {
        (void)tk_del_tsk(h_task);
        saved          = c3task_resource_enter();
        g_reaper_state = C3TASK_REAPER_FAILED;
        c3task_resource_leave(saved);
        return -1;
    }

    saved          = c3task_resource_enter();
    g_reaper_state = C3TASK_REAPER_READY;
    c3task_resource_leave(saved);
    return 0;
#endif
#endif
}

/** @brief Return the selected static stack-slot capacity. */
uint32_t c3task_capacity (void)
{
    return C3TASK_STACK_SLOTS;
}

/** @brief Count occupied stack slots under one consistent snapshot. */
uint32_t c3task_used (void)
{
    uint32_t       used  = 0U;
    const uint32_t saved = c3task_resource_enter();

#if 0U < C3TASK_STACK_SLOTS
    for (uint32_t index = 0U; index < C3TASK_STACK_SLOTS; ++index)
    {
        if (g_stack_slots[index].b_used)
        {
            ++used;
        }
    }
#endif

    c3task_resource_leave(saved);
    return used;
}

/** @brief Bind exactly one shim cleanup implementation. */
int c3task_reaper_bind (c3task_reap_t * p_reap)
{
#if 0U == C3TASK_STACK_SLOTS
    (void)p_reap;
    return -1;
#else
    int            result = -1;
    const uint32_t saved  = c3task_resource_enter();

    if ((NULL != p_reap) && ((NULL == gp_reap) || (p_reap == gp_reap)))
    {
        gp_reap = p_reap;
        result  = 0;
    }

    c3task_resource_leave(saved);
    return result;
#endif
}

/** @brief Reserve the smallest suitable fixed stack. */
int c3task_stack_acquire (uintptr_t  owner,
                          uint32_t   requested_bytes,
                          void **    pp_stack,
                          uint32_t * p_capacity_bytes)
{
#if 0U == C3TASK_STACK_SLOTS
    (void)owner;
    (void)requested_bytes;
    (void)pp_stack;
    (void)p_capacity_bytes;
    return -1;
#else
    if ((0U == owner) || (0U == requested_bytes) || (NULL == pp_stack) ||
        (NULL == p_capacity_bytes) ||
        (requested_bytes > (UINT32_MAX - (C3TASK_STACK_ALIGNMENT - 1U))))
    {
        return -1;
    }

    const uint32_t aligned = (requested_bytes + (C3TASK_STACK_ALIGNMENT - 1U)) &
                             ~(C3TASK_STACK_ALIGNMENT - 1U);
    const uint32_t saved = c3task_resource_enter();

    for (uint32_t index = 0U; index < C3TASK_STACK_SLOTS; ++index)
    {
        if ((g_stack_slots[index].b_used) &&
            (owner == g_stack_slots[index].owner))
        {
            c3task_resource_leave(saved);
            return -1;
        }
    }

    const int32_t selected = c3task_stack_best_fit(aligned);

    if (0 <= selected)
    {
        const uint32_t index   = (uint32_t)selected;
        void * const   p_stack = c3task_stack_at(index);

        if ((NULL != p_stack) &&
            (0U == ((uintptr_t)p_stack & (C3TASK_STACK_ALIGNMENT - 1U))))
        {
            g_stack_slots[index].owner  = owner;
            g_stack_slots[index].b_used = true;
            *pp_stack                   = p_stack;
            *p_capacity_bytes           = c3task_stack_capacity_at(index);
            c3task_resource_leave(saved);
            return 0;
        }
    }

    c3task_resource_leave(saved);
    return -1;
#endif
}

/** @brief Release an exactly matched stack only after kernel deletion. */
int c3task_stack_release (uintptr_t owner,
                          void *    p_stack,
                          bool      b_kernel_deleted)
{
#if 0U == C3TASK_STACK_SLOTS
    (void)owner;
    (void)p_stack;
    (void)b_kernel_deleted;
    return -1;
#else
    if ((0U == owner) || (NULL == p_stack) || (!b_kernel_deleted))
    {
        return -1;
    }

    int            result = -1;
    const uint32_t saved  = c3task_resource_enter();

    for (uint32_t index = 0U; index < C3TASK_STACK_SLOTS; ++index)
    {
        if ((g_stack_slots[index].b_used) &&
            (owner == g_stack_slots[index].owner) &&
            (p_stack == c3task_stack_at(index)))
        {
            g_stack_slots[index] = (c3task_stack_slot_t){0};
            result               = 0;
            break;
        }
    }

    c3task_resource_leave(saved);
    return result;
#endif
}

#if 0U < C3TASK_STACK_SLOTS
/** @brief Return one fixed slot's capacity. */
static uint32_t c3task_stack_capacity_at (uint32_t index)
{
    static const uint32_t capacities[C3TASK_STACK_SLOTS] = {
        C3TASK_STACK_0_BYTES,
        C3TASK_STACK_1_BYTES,
        C3TASK_STACK_2_BYTES,
#if 4U == C3TASK_STACK_SLOTS
        C3TASK_STACK_3_BYTES
#endif
    };
    return (index < C3TASK_STACK_SLOTS) ? capacities[index] : 0U;
}

/** @brief Return one fixed slot's storage address. */
static void * c3task_stack_at (uint32_t index)
{
    void * p_stack = NULL;

    switch (index)
    {
    case 0U:
        p_stack = g_stack_0;
    break;

    case 1U:
        p_stack = g_stack_1;
    break;

    case 2U:
        p_stack = g_stack_2;
    break;

#if 4U == C3TASK_STACK_SLOTS
    case 3U:
        p_stack = g_stack_3;
    break;
#endif

    default:
    break;
    }

    return p_stack;
}

/** @brief Find the smallest free slot meeting a rounded byte request. */
static int32_t c3task_stack_best_fit (uint32_t requested_bytes)
{
    int32_t  selected          = -1;
    uint32_t selected_capacity = UINT32_MAX;

    for (uint32_t index = 0U; index < C3TASK_STACK_SLOTS; ++index)
    {
        const uint32_t capacity = c3task_stack_capacity_at(index);

        if ((!g_stack_slots[index].b_used) && (requested_bytes <= capacity) &&
            (capacity < selected_capacity))
        {
            selected          = (int32_t)index;
            selected_capacity = capacity;
        }
    }

    return selected;
}

#if !C3TASK_HOST_BUILD
/** @brief Reclaim bounded shim resources from a surviving low-priority task. */
static void c3task_reaper_task (INT code, void * p_context)
{
    (void)code;
    (void)p_context;

    for (;;)
    {
        c3task_reap_t * p_reap = NULL;
        const uint32_t  saved  = c3task_resource_enter();

        p_reap = gp_reap;
        c3task_resource_leave(saved);

        if (NULL != p_reap)
        {
            for (uint32_t attempt = 0U; attempt < C3TASK_STACK_SLOTS; ++attempt)
            {
                if (0 >= p_reap())
                {
                    break;
                }
            }
        }

        (void)tk_dly_tsk((TMO)C3TASK_REAPER_PERIOD_MS);
    }
}
#endif
#endif

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

