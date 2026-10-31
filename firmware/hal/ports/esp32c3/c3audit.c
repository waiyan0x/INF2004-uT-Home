/**
 * @file c3audit.c
 * @brief Count events with bounded single-core interrupt exclusion.
 */
#include "c3audit.h"
#include "workport.h"
#include "stackmon.h"
#include <tk/syslib.h>
#include <tk/dbgspt.h>

#define C3AUDIT_BOOT_STACK_BYTES 8448U

static memaudit_t g_state            = {0};
static void *     gp_boot_stack      = NULL;
static uint32_t   g_boot_allocations = 0U;
static void record_create (uint32_t kind);
static void record_kernel (uint32_t kind);

/** @brief Count a successful SDK allocation, including pre-kernel startup. */
void c3audit_alloc (void * p_memory, size_t size, uint32_t capabilities)
{
    /* Capabilities are not a permission to exempt any allocation. */
    (void)capabilities;
    if (NULL != p_memory)
    {
        const UW interrupt_state = disint();
        /* The private state is always valid; recording cannot fail. */
        (void)memaudit_heap(&g_state, size, false);
        enaint(interrupt_state);
    }
}

/** @brief Count non-null SDK free events without reading freed memory. */
void c3audit_free (void * p_memory)
{
    if (NULL != p_memory)
    {
        const UW interrupt_state = disint();
        /* The private state is always valid; recording cannot fail. */
        (void)memaudit_heap(&g_state, 0U, true);
        enaint(interrupt_state);
    }
}

/** @brief Mark serialized application entry before any of its initialization.
 */
int c3audit_begin (void)
{
    const UW  interrupt_state = disint();
    const int result          = memaudit_begin(&g_state);
    enaint(interrupt_state);
    return result;
}

/** @brief Seal once before restore enters normal configuration operation. */
int c3audit_seal (void)
{
    if (!rt_port_is_owner())
    {
        return -1;
    }
    const UW  interrupt_state = disint();
    const int result          = memaudit_seal(&g_state);
    enaint(interrupt_state);
    return result;
}

/** @brief Copy a bounded coherent snapshot in the controller context. */
int c3audit_capture (memaudit_t * p_state)
{
    if ((NULL == p_state) || (!rt_port_is_owner()))
    {
        return -1;
    }
    const UW interrupt_state = disint();
    *p_state                 = g_state;
    enaint(interrupt_state);
    return 0;
}

/** @brief Count the kernel allocator entry without changing signed sizes. */
void * c3audit_kmalloc (SZ size)
{
    record_kernel(0U);
    void *   p_memory        = c3audit_native_malloc(size);
    const UW interrupt_state = disint();
    if ((!g_state.b_application) && (NULL != p_memory))
    {
        if (g_boot_allocations < UINT32_MAX)
        {
            ++g_boot_allocations;
        }
        gp_boot_stack = NULL;
        if ((1U == g_boot_allocations) && (8448 == size) &&
            (0 == stackmon_paint(p_memory, C3AUDIT_BOOT_STACK_BYTES)))
        {
            /* The block is still exclusively owned by its allocator caller.
             * Native task-context construction happens after this returns. */
            gp_boot_stack = p_memory;
        }
    }
    enaint(interrupt_state);
    return p_memory;
}

/** @brief Bound the controller scan while it cannot be preempted. */
int c3audit_stack_unused (uint32_t * p_unused)
{
    if ((NULL == p_unused) || (!rt_port_is_owner()))
    {
        return -1;
    }
    TD_RTSK task = {0};
    if ((E_OK != td_ref_tsk(TSK_SELF, &task)) || (8448 != task.stksz))
    {
        return -1;
    }
    size_t   unused          = 0U;
    const UW interrupt_state = disint();
    int      result          = -1;
    if (1U == g_boot_allocations)
    {
        const unsigned char * p_base = gp_boot_stack;
        if ((NULL != p_base) &&
            (task.istack == (p_base + C3AUDIT_BOOT_STACK_BYTES)))
        {
            result = stackmon_unused(p_base, C3AUDIT_BOOT_STACK_BYTES, &unused);
        }
    }
    enaint(interrupt_state);
    if (0 == result)
    {
        /* The scanner bounds its result by the 8448-byte kernel reservation. */
        *p_unused = (uint32_t)unused;
    }
    return result;
}

/** @brief Count calloc as one entry; its internal malloc remains native. */
void * c3audit_kcalloc (SZ count, SZ size)
{
    record_kernel(1U);
    return c3audit_native_calloc(count, size);
}

/** @brief Preserve native realloc behavior, including failed attempts. */
void * c3audit_krealloc (void * p_memory, SZ size)
{
    record_kernel(2U);
    return c3audit_native_realloc(p_memory, size);
}

/** @brief Count free entry without dereferencing the pointer. */
void c3audit_kfree (void * p_memory)
{
    record_kernel(3U);
    c3audit_native_free(p_memory);
}

/** @brief Count then preserve native task creation behavior. */
ID c3audit_create_task (const T_CTSK * p_config)
{
    record_create(UTHOME_AUDIT_TASK);
    return c3audit_native_task(p_config);
}

/** @brief Count then preserve native mutex creation behavior. */
ID c3audit_create_mutex (const T_CMTX * p_config)
{
    record_create(UTHOME_AUDIT_MUTEX);
    return c3audit_native_mutex(p_config);
}

/** @brief Count then preserve native sem creation behavior. */
ID c3audit_create_sem (const T_CSEM * p_config)
{
    record_create(UTHOME_AUDIT_SEM);
    return c3audit_native_sem(p_config);
}

/** @brief Count then preserve native flag creation behavior. */
ID c3audit_create_flag (const T_CFLG * p_config)
{
    record_create(UTHOME_AUDIT_FLAG);
    return c3audit_native_flag(p_config);
}

/** @brief Count then preserve native mailbox creation behavior. */
ID c3audit_create_mailbox (const T_CMBX * p_config)
{
    record_create(UTHOME_AUDIT_MAILBOX);
    return c3audit_native_mailbox(p_config);
}

/** @brief Count then preserve native message creation behavior. */
ID c3audit_create_message (const T_CMBF * p_config)
{
    record_create(UTHOME_AUDIT_MESSAGE);
    return c3audit_native_message(p_config);
}

/** @brief Count then preserve native fixed pool creation behavior. */
ID c3audit_create_fixed_pool (const T_CMPF * p_config)
{
    record_create(UTHOME_AUDIT_FIXED_POOL);
    return c3audit_native_fixed_pool(p_config);
}

/** @brief Count then preserve native pool creation behavior. */
ID c3audit_create_pool (const T_CMPL * p_config)
{
    record_create(UTHOME_AUDIT_POOL);
    return c3audit_native_pool(p_config);
}

/** @brief Count then preserve native port creation behavior. */
ID c3audit_create_port (const T_CPOR * p_config)
{
    record_create(UTHOME_AUDIT_PORT);
    return c3audit_native_port(p_config);
}

/** @brief Count then preserve native cycle creation behavior. */
ID c3audit_create_cycle (const T_CCYC * p_config)
{
    record_create(UTHOME_AUDIT_CYCLE);
    return c3audit_native_cycle(p_config);
}

/** @brief Count then preserve native alarm creation behavior. */
ID c3audit_create_alarm (const T_CALM * p_config)
{
    record_create(UTHOME_AUDIT_ALARM);
    return c3audit_native_alarm(p_config);
}

/** @brief Record a fixed valid kind before entering the native kernel. */
static void record_create (uint32_t kind)
{
    const UW interrupt_state = disint();
    /* All private callers supply one of the eleven constant kind IDs. */
    (void)memaudit_create(&g_state, kind);
    enaint(interrupt_state);
}

/** @brief Serialize a known allocator kind with every other audit event. */
static void record_kernel (uint32_t kind)
{
    const UW interrupt_state = disint();
    /* Private callers supply one of the four fixed allocator IDs. */
    (void)memaudit_kernel(&g_state, kind);
    enaint(interrupt_state);
}

/* End of file. */

