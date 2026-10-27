/**
 * @file c3nq.c
 * @brief Fixed networking queues over startup-only uT-Kernel resources.
 */
#include "c3nq.h"
#include "c3event.h"

#include "netpool.h"
#include "esp_timer.h"
#include "esp_private/cache_utils.h"
#include "sdkconfig.h"
#include <tk/tkernel.h>
#include <tk/syslib.h>

typedef struct
{
    ID h_readable;
    ID h_writable;
    ID h_mutex;
} c3nq_native_t;

static netpool_t     g_pool                  = {0};
static c3nq_native_t g_native[NETPOOL_SLOTS] = {0};
static bool          gb_attempted            = false;
static bool          gb_task_started         = false;

#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
static c3nq_diagnostic_t g_diagnostic = {0};
#endif

static uint32_t enter (void * p_context);
static void leave (void * p_context, uint32_t saved);
static int clock_us (void * p_context, uint64_t * p_now_us);
static bool can_block (void * p_context);
static uint32_t current_owner (void * p_context);
static int wait_event (void *   p_context,
                       uint32_t slot,
                       bool     b_sender,
                       uint32_t milliseconds);
static void signal_event (void * p_context, uint32_t slot, bool b_sender);
static int lock_mutex (void * p_context, uint32_t slot, uint32_t milliseconds);
static int unlock_mutex (void * p_context, uint32_t slot);
static TMO native_timeout (uint32_t milliseconds);
static int32_t sdk_result (int result);
static void * create (uint32_t  length,
                      uint32_t  item_bytes,
                      uint8_t   kind,
                      uint8_t * p_storage,
                      void *    p_key,
                      bool      b_static);

/** @brief Reserve permanent notification semaphores and inherited mutexes. */
int c3nq_init (void)
{
    const netpool_ops_t ops = {.p_context   = NULL,
                               .p_enter     = enter,
                               .p_leave     = leave,
                               .p_clock     = clock_us,
                               .p_can_block = can_block,
                               .p_owner     = current_owner,
                               .p_wait      = wait_event,
                               .p_signal    = signal_event,
                               .p_lock      = lock_mutex,
                               .p_unlock    = unlock_mutex};
    const T_CSEM semaphore  = {.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1};
    const T_CMTX mutex      = {.mtxatr = TA_INHERIT};

    if (gb_attempted)
    {
        return g_pool.b_ready ? 0 : -1;
    }
    gb_attempted = true;
    for (uint32_t index = 0U; index < NETPOOL_SLOTS; ++index)
    {
        bool b_failed = false;
        if (index < NETPOOL_QUEUE_SLOTS)
        {
            g_native[index].h_readable = tk_cre_sem(&semaphore);
            g_native[index].h_writable = tk_cre_sem(&semaphore);
            b_failed                   = ((g_native[index].h_readable <= 0) ||
                        (g_native[index].h_writable <= 0));
        }
        else if (index < (NETPOOL_QUEUE_SLOTS + NETPOOL_SEMAPHORE_SLOTS))
        {
            g_native[index].h_readable = tk_cre_sem(&semaphore);
            b_failed                   = (g_native[index].h_readable <= 0);
        }
        else
        {
            g_native[index].h_mutex = tk_cre_mtx(&mutex);
            b_failed                = (g_native[index].h_mutex <= 0);
        }
        if (b_failed)
        {
            /* Startup fails; partial permanent resources are retained. */
            return -1;
        }
    }
    return netpool_init(&g_pool, &ops, CONFIG_FREERTOS_HZ);
}

/**
 * @brief Extend the pinned early native-table initialization before libc.
 *
 * The vendor's guarded priority-one hook calls this wrapper in the selected
 * build. The source-scoped native rename invokes the real initializer without
 * recursion. Its second guarded call must not reset tables or reserve resources
 * again.
 */
#if defined(UTHOME_STATIC_NET_QUEUES) && UTHOME_STATIC_NET_QUEUES
int32_t c3nq_bootstrap (void)
{
    const int32_t result = c3nq_native_tables();
    if (0 != result)
    {
        return result;
    }
    return ((0 == c3nq_init()) && (0 == c3event_init())) ? E_OK : E_NOMEM;
}
#endif

/** @brief SDK dynamic API leases fixed application storage, never heap. */
void * c3nq_create (uint32_t length, uint32_t item_bytes, uint8_t kind)
{
    void * p_handle = create(length, item_bytes, kind, NULL, NULL, false);
#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
    const UW saved = disint();
    if (UINT32_MAX > g_diagnostic.calls)
    {
        ++g_diagnostic.calls;
    }
    if (NULL == p_handle)
    {
        if (UINT32_MAX > g_diagnostic.failures)
        {
            ++g_diagnostic.failures;
        }
        g_diagnostic.length     = length;
        g_diagnostic.item_bytes = item_bytes;
        g_diagnostic.kind       = kind;
    }
    enaint(saved);
#endif
    return p_handle;
}

#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
/** @brief Snapshot without logging or heap calls in the queue adapter. */
int c3nq_diagnostic (c3nq_diagnostic_t * p_report)
{
    if (NULL == p_report)
    {
        return -1;
    }
    const UW saved = disint();
    *p_report      = g_diagnostic;
    enaint(saved);
    return 0;
}
#endif

/** @brief Static APIs retain caller storage and use its opaque control key. */
void * c3nq_static (uint32_t  length,
                    uint32_t  item_bytes,
                    uint8_t * p_storage,
                    void *    p_key,
                    uint8_t   kind)
{
    return create(length, item_bytes, kind, p_storage, p_key, true);
}

/** @brief Counting semaphore limits are explicit, not queue payload sizes. */
void * c3nq_counting (uint32_t maximum, uint32_t initial)
{
    return c3nq_create(maximum, initial, 2U);
}

/** @brief Bind a caller's static counting semaphore identity. */
void * c3nq_counting_static (uint32_t maximum, uint32_t initial, void * p_key)
{
    return c3nq_static(maximum, initial, NULL, p_key, 2U);
}

/** @brief Reject unknown mutex kinds before reserving a lease. */
void * c3nq_mutex (uint8_t kind)
{
    return ((1U == kind) || (4U == kind)) ? c3nq_create(1U, 0U, kind) : NULL;
}

/** @brief Static mutex identity shares permanent native priority inheritance.
 */
void * c3nq_mutex_static (uint8_t kind, void * p_key)
{
    return ((1U == kind) || (4U == kind))
               ? c3nq_static(1U, 0U, NULL, p_key, kind)
               : NULL;
}

/** @brief Send at front/back or replace a one-item queue. */
int32_t c3nq_send (void *       p_handle,
                   void const * p_item,
                   uint32_t     ticks,
                   int32_t      position)
{
    if ((!spi_flash_cache_enabled()) || (position < 0) || (position > 2))
    {
        return 0;
    }
    return sdk_result(netpool_send(
        &g_pool, p_handle, p_item, ticks, (netring_position_t)position, false));
}

/** @brief ISR sends poll; preserve an already-set scheduler hint. */
int32_t c3nq_send_isr (void *       p_handle,
                       void const * p_item,
                       int32_t *    p_woken,
                       int32_t      position)
{
    int32_t result = 0;
    if ((spi_flash_cache_enabled()) && (0 <= position) && (2 >= position))
    {
        result = sdk_result(netpool_send(
            &g_pool, p_handle, p_item, 0U, (netring_position_t)position, true));
    }
    if ((0 != result) && (NULL != p_woken))
    {
        /* Conservative hint: the native ISR tail decides actual dispatch. */
        *p_woken = 1;
    }
    return result;
}

/** @brief Receive with one absolute wait budget. */
int32_t c3nq_receive (void * p_handle, void * p_item, uint32_t ticks)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(
        netpool_receive(&g_pool, p_handle, p_item, ticks, false, false));
}

/** @brief ISR receive never calls a blocking native operation. */
int32_t c3nq_receive_isr (void * p_handle, void * p_item, int32_t * p_woken)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    const int32_t result =
        sdk_result(netpool_receive(&g_pool, p_handle, p_item, 0U, false, true));
    if ((0 != result) && (NULL != p_woken))
    {
        *p_woken = 1;
    }
    return result;
}

/** @brief Peek retains data and the receiver notification opportunity. */
int32_t c3nq_peek (void * p_handle, void * p_item, uint32_t ticks)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(
        netpool_receive(&g_pool, p_handle, p_item, ticks, true, false));
}

/** @brief ISR peek retains queue state and never blocks. */
int32_t c3nq_peek_isr (void * p_handle, void * p_item)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(
        netpool_receive(&g_pool, p_handle, p_item, 0U, true, true));
}

/** @brief Take a semaphore/mutex or explicitly discard a queue item. */
int32_t c3nq_take (void * p_handle, uint32_t ticks)
{
    return c3nq_receive(p_handle, NULL, ticks);
}

/** @brief Give from ISR; mutexes are rejected by the pool. */
int32_t c3nq_give_isr (void * p_handle, int32_t * p_woken)
{
    return c3nq_send_isr(p_handle, NULL, p_woken, 0);
}

/** @brief Recursive mutex take keeps one underlying native acquisition. */
int32_t c3nq_take_recursive (void * p_handle, uint32_t ticks)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(netpool_mutex(&g_pool, p_handle, ticks, true, true));
}

/** @brief Recursive give releases the native mutex only at depth zero. */
int32_t c3nq_give_recursive (void * p_handle)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(netpool_mutex(&g_pool, p_handle, 0U, false, true));
}

/** @brief Never delete a native object or free caller storage. */
void c3nq_delete (void * p_handle)
{
    /* Refused deletion retains the lease rather than invalidating waiters. */
    (void)netpool_release(&g_pool, p_handle);
}

/** @brief Unknown handles report no data and are never dereferenced. */
uint32_t c3nq_waiting (void * p_handle)
{
    uint32_t       count = 0U;
    uint32_t       space = 0U;
    netpool_kind_t kind  = NETPOOL_QUEUE;
    (void)netpool_query(&g_pool, p_handle, &count, &space, &kind);
    return count;
}

/** @brief Return the entire configured capacity minus occupancy. */
uint32_t c3nq_spaces (void * p_handle)
{
    uint32_t       count = 0U;
    uint32_t       space = 0U;
    netpool_kind_t kind  = NETPOOL_QUEUE;
    (void)netpool_query(&g_pool, p_handle, &count, &space, &kind);
    return space;
}

/** @brief Coherent empty observation from a task or ISR. */
int32_t c3nq_empty_isr (void * p_handle)
{
    return (0U == c3nq_waiting(p_handle)) ? 1 : 0;
}

/** @brief Reset queue occupancy while keeping native resources. */
int32_t c3nq_reset (void * p_handle)
{
    if (!spi_flash_cache_enabled())
    {
        return 0;
    }

    return sdk_result(netpool_reset(&g_pool, p_handle));
}

/** @brief The SDK reset macro reaches this separate linker symbol. */
int32_t c3nq_generic_reset (void * p_handle, int32_t new_queue)
{
    return (0 == new_queue) ? c3nq_reset(p_handle) : 0;
}

/** @brief Match the pinned SDK state constants, including pre-kernel boot. */
int32_t c3nq_scheduler_state (void)
{
    T_RSYS state = {0};
    if (!spi_flash_cache_enabled())
    {
        return 0; /* SDK taskSCHEDULER_SUSPENDED. */
    }
    if ((!g_pool.b_ready) || (E_OK != tk_ref_sys(&state)) ||
        (0 == state.runtskid))
    {
        return 1; /* SDK taskSCHEDULER_NOT_STARTED. */
    }
    return (0U != (state.sysstat & TSS_DDSP)) ? 0 : 2;
}

/** @brief Single-core saved interrupt state protects metadata and payloads. */
static uint32_t enter (void * p_context)
{
    (void)p_context;
    return disint();
}

/** @brief Restore exactly the caller's interrupt mask, including ISR entry. */
static void leave (void * p_context, uint32_t saved)
{
    (void)p_context;
    enaint(saved);
}

/** @brief SDK microseconds are monotonic and independent of kernel ticks. */
static int clock_us (void * p_context, uint64_t * p_now_us)
{
    (void)p_context;
    const int64_t now_us = esp_timer_get_time();
    if ((NULL == p_now_us) || (0 > now_us))
    {
        return -1;
    }
    *p_now_us = (uint64_t)now_us;
    return 0;
}

/** @brief Block only in ordinary task context with dispatch/interrupts enabled.
 */
static bool can_block (void * p_context)
{
    (void)p_context;
    T_RSYS state = {0};
    return ((E_OK == tk_ref_sys(&state)) && (TSS_TSK == state.sysstat));
}

/** @brief Distinguish serialized SDK startup from native task ownership. */
static uint32_t current_owner (void * p_context)
{
    (void)p_context;
    T_RSYS         state = {0};
    uint32_t       owner = 0U;
    const uint32_t saved = disint();
    if ((g_pool.b_ready) && (E_OK == tk_ref_sys(&state)))
    {
        if (0 < state.runtskid)
        {
            gb_task_started = true;
            if (0U == (state.sysstat & TSS_INDP))
            {
                owner = (uint32_t)state.runtskid;
            }
        }
        else if (!gb_task_started)
        {
            owner = NETPOOL_BOOT_OWNER;
        }
        else
        {
            /* Never return to boot ownership after a native task exists. */
        }
    }
    enaint(saved);
    return owner;
}

/** @brief Notifications may be stale; the portable layer always rechecks data.
 */
static int wait_event (void *   p_context,
                       uint32_t slot,
                       bool     b_sender,
                       uint32_t milliseconds)
{
    (void)p_context;
    if (NETPOOL_SLOTS <= slot)
    {
        return -1;
    }
    const ID h_object =
        b_sender ? g_native[slot].h_writable : g_native[slot].h_readable;
    const ER result = tk_wai_sem(h_object, 1, native_timeout(milliseconds));
    return (E_OK == result) ? 0 : ((E_TMOUT == result) ? 1 : -1);
}

/** @brief Full notification is harmless; authoritative data stays in the pool.
 */
static void signal_event (void * p_context, uint32_t slot, bool b_sender)
{
    (void)p_context;
    if (slot < NETPOOL_SLOTS)
    {
        const ID h_object =
            b_sender ? g_native[slot].h_writable : g_native[slot].h_readable;
        (void)tk_sig_sem(h_object, 1);
    }
}

/** @brief Native priority inheritance is retained for all mutex leases. */
static int lock_mutex (void * p_context, uint32_t slot, uint32_t milliseconds)
{
    (void)p_context;
    if (NETPOOL_SLOTS <= slot)
    {
        return -1;
    }
    const ER result =
        tk_loc_mtx(g_native[slot].h_mutex, native_timeout(milliseconds));
    return (E_OK == result) ? 0 : ((E_TMOUT == result) ? 1 : -1);
}

/** @brief Native unlock and metadata publication occur under one outer guard.
 */
static int unlock_mutex (void * p_context, uint32_t slot)
{
    (void)p_context;
    return ((slot < NETPOOL_SLOTS) &&
            (E_OK == tk_unl_mtx(g_native[slot].h_mutex)))
               ? 0
               : -1;
}

/** @brief Preserve infinite/poll semantics and reject signed overflow. */
static TMO native_timeout (uint32_t milliseconds)
{
    if (UINT32_MAX == milliseconds)
    {
        return TMO_FEVR;
    }
    return (INT32_MAX >= milliseconds) ? (TMO)milliseconds : TMO_POL;
}

/** @brief Convert owned zero-success convention to the SDK Boolean result. */
static int32_t sdk_result (int result)
{
    return (0 == result) ? 1 : 0;
}

/** @brief Translate the pinned FreeRTOS queue-kind ABI before borrowing a slot.
 */
static void * create (uint32_t  length,
                      uint32_t  item_bytes,
                      uint8_t   kind,
                      uint8_t * p_storage,
                      void *    p_key,
                      bool      b_static)
{
    netpool_kind_t pool_kind     = NETPOOL_QUEUE;
    uint32_t       initial       = 0U;
    size_t         storage_bytes = 0U;

    if ((b_static) && (NULL == p_key))
    {
        return NULL;
    }
    switch (kind)
    {
    case 0U:
        if (0U == item_bytes)
        {
            pool_kind = NETPOOL_SEMAPHORE;
            length    = 1U;
        }
        else if (((b_static) && (NULL == p_storage)) ||
                 (length > (SIZE_MAX / item_bytes)))
        {
            return NULL;
        }
        else
        {
            storage_bytes = (size_t)length * item_bytes;
        }
    break;

    case 1U:
    case 4U:
        pool_kind  = (1U == kind) ? NETPOOL_MUTEX : NETPOOL_RECURSIVE;
        length     = 1U;
        item_bytes = 0U;
    break;

    case 2U:
        pool_kind  = NETPOOL_SEMAPHORE;
        initial    = item_bytes;
        item_bytes = 0U;
    break;

    case 3U:
        pool_kind  = NETPOOL_SEMAPHORE;
        length     = 1U;
        item_bytes = 0U;
    break;

    default:
        return NULL;
    break;
    }
    return netpool_create(&g_pool,
                          pool_kind,
                          length,
                          item_bytes,
                          initial,
                          p_storage,
                          storage_bytes,
                          p_key);
}

/* End of file. */

