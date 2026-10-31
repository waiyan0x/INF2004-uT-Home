#!/usr/bin/env python3
"""Exercise lifecycle code extracted from the actually applied shim patch."""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
VENDOR_SHIM = (
    ROOT
    / "firmware/third_party/esp32c3-mtk3/components/freertos_shim/shim_task.c"
)
PATCH = ROOT / "tools/target/patches/freertos-task-observer.patch"


def extract_function(source, name):
    pattern = (
        r"\b(?:static\s+)?[A-Za-z_][\w\s*]*\b"
        + re.escape(name)
        + r"\s*\([^;{}]*\)\s*\{"
    )
    found = re.search(pattern, source)
    if found is None:
        raise RuntimeError(f"Missing function in patched source: {name}")
    depth = 0
    for index in range(found.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[found.start(): index + 1]
    raise RuntimeError(f"Unterminated function in patched source: {name}")


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef int ID;
typedef int ER;
typedef int INT;
typedef int SZ;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned UINT;
typedef unsigned char UB;
typedef void (*TaskFunction_t)(void *);
typedef void * TaskHandle_t;
typedef void (*FP)(void *);
typedef int shim_tag_t;

typedef struct
{
    shim_tag_t tag;
} shim_hdr_t;

typedef struct
{
    shim_hdr_t       hdr;
    ID               tsk_id;
    ID               notify_sem;
    volatile uint32_t notify_val;
    void *           stack;
    uint32_t         stack_sz;
    char             name[12];
    void *           tls[1];
} shim_task_t;

typedef struct
{
    int sematr;
    int isemcnt;
    int maxsem;
} T_CSEM;

typedef struct
{
    void * exinf;
    int    tskatr;
    FP     task;
    int    itskpri;
    SZ     stksz;
    void * bufptr;
} T_CTSK;

typedef struct
{
    int tskstat;
} T_RTSK;

typedef struct
{
    int32_t  hi;
    uint32_t lo;
} SYSTIM;

#define SHIM_TASK_MAX      4
#define SHIM_TASK_RESERVED ((ID)-1)
#define SHIM_FLAG_CLEANUP_BUSY 0x01U
#define SHIM_FLAG_OBSERVED     0x02U
#define SHIM_FLAG_SELF_EXIT    0x04U
#define SHIM_FLAG_NOTIFY_PENDING 0x08U
#define pdFALSE            0
#define pdTRUE             1
#define E_OK               0
#define E_NOEXS            (-42)
#define TTS_DMT            1
#define TTS_RDY            2
#define TA_TFIFO           1
#define TA_HLNG            2
#define TA_RNG0            4
#define TA_USERBUF         8

static pthread_mutex_t g_shim_guard = PTHREAD_MUTEX_INITIALIZER;
#define DI(saved)                                                             \
    do                                                                        \
    {                                                                         \
        (saved) = 0U;                                                         \
        assert(0 == pthread_mutex_lock(&g_shim_guard));                        \
    } while (0)
#define EI(saved)                                                             \
    do                                                                        \
    {                                                                         \
        (void)(saved);                                                        \
        assert(0 == pthread_mutex_unlock(&g_shim_guard));                      \
    } while (0)

@@STATE_ENUM@@

static shim_task_t       s_tasks[SHIM_TASK_MAX];
static uint8_t           s_task_states[SHIM_TASK_MAX];
static uint8_t           s_task_flags[SHIM_TASK_MAX];
static uintptr_t         s_handles[SHIM_TASK_MAX];
static uint32_t          s_generations[SHIM_TASK_MAX];

int shim_task_reap_one (void);

typedef union
{
    long double alignment;
    uint8_t     bytes[2816];
} stack_2816_t;

typedef union
{
    long double alignment;
    uint8_t     bytes[3584];
} stack_3584_t;

typedef union
{
    long double alignment;
    uint8_t     bytes[4096];
} stack_4096_t;

typedef union
{
    long double alignment;
    uint8_t     bytes[6656];
} stack_6656_t;

static stack_2816_t g_stack_2816;
static stack_3584_t g_stack_3584;
static stack_4096_t g_stack_4096;
static stack_6656_t g_stack_6656;
static void * const gp_stacks[4] = {g_stack_2816.bytes,
                                    g_stack_3584.bytes,
                                    g_stack_4096.bytes,
                                    g_stack_6656.bytes};
static const uint32_t g_capacities[4] = {2816U, 3584U, 4096U, 6656U};
static uintptr_t      g_stack_owners[4];
static pthread_mutex_t g_pool_guard = PTHREAD_MUTEX_INITIALIZER;
static int             g_fail_stack_release;
static int             g_pool_sealed;
static int (*gp_bound_reaper)(void);

static int c3task_reaper_bind (int (*p_reap)(void))
{
    if ((NULL == p_reap) ||
        ((NULL != gp_bound_reaper) && (p_reap != gp_bound_reaper)))
    {
        return -1;
    }
    gp_bound_reaper = p_reap;
    return 0;
}

static int c3task_stack_acquire (uintptr_t  owner,
                                 uint32_t   requested,
                                 void **    pp_stack,
                                 uint32_t * p_capacity)
{
    int      selected = -1;
    uint32_t best     = UINT32_MAX;

    assert(0 == pthread_mutex_lock(&g_pool_guard));
    if (g_pool_sealed)
    {
        assert(0 == pthread_mutex_unlock(&g_pool_guard));
        return -1;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (owner == g_stack_owners[index])
        {
            assert(0 == pthread_mutex_unlock(&g_pool_guard));
            return -1;
        }
        if ((0U == g_stack_owners[index]) &&
            (requested <= g_capacities[index]) &&
            (g_capacities[index] < best))
        {
            selected = index;
            best     = g_capacities[index];
        }
    }
    if (0 <= selected)
    {
        g_stack_owners[selected] = owner;
        *pp_stack                = gp_stacks[selected];
        *p_capacity              = g_capacities[selected];
    }
    assert(0 == pthread_mutex_unlock(&g_pool_guard));
    return (0 <= selected) ? 0 : -1;
}

static int c3task_stack_release (uintptr_t owner,
                                 void *    p_stack,
                                 int       b_kernel_deleted)
{
    int result = -1;

    assert(0 == pthread_mutex_lock(&g_pool_guard));
    if (g_fail_stack_release > 0)
    {
        --g_fail_stack_release;
        assert(0 == pthread_mutex_unlock(&g_pool_guard));
        return -1;
    }
    if (b_kernel_deleted)
    {
        for (int index = 0; index < 4; ++index)
        {
            if ((owner == g_stack_owners[index]) &&
                (p_stack == gp_stacks[index]))
            {
                g_stack_owners[index] = 0U;
                result                = 0;
                break;
            }
        }
    }
    assert(0 == pthread_mutex_unlock(&g_pool_guard));
    return result;
}

static int c3task_stack_commit (uintptr_t owner, void * p_stack)
{
    for (int index = 0; index < 4; ++index)
    {
        if ((owner == g_stack_owners[index]) &&
            (p_stack == gp_stacks[index]))
        {
            return 0;
        }
    }
    return -1;
}

static void c3task_cleanup_pending (uintptr_t owner, uint32_t now_ms)
{
    assert(0U != owner);
    (void)now_ms;
}

static void c3task_cleanup_complete (uintptr_t owner, uint32_t now_ms)
{
    assert(0U != owner);
    (void)now_ms;
}

static int stack_count (void)
{
    int count = 0;
    assert(0 == pthread_mutex_lock(&g_pool_guard));
    for (int index = 0; index < 4; ++index)
    {
        if (0U != g_stack_owners[index])
        {
            ++count;
        }
    }
    assert(0 == pthread_mutex_unlock(&g_pool_guard));
    return count;
}

static pthread_mutex_t g_observer_guard = PTHREAD_MUTEX_INITIALIZER;
static uint32_t        g_next_observer_token = 1U;
static int             g_observer_pending;
static int             g_observer_active;
static int             g_fail_observer_cancel;
static int             g_fail_observer_release;

static uint32_t c3task_observer_begin (const char * p_name,
                                       uint32_t     stack_bytes)
{
    (void)p_name;
    (void)stack_bytes;
    assert(0 == pthread_mutex_lock(&g_observer_guard));
    const uint32_t token = g_next_observer_token++;
    ++g_observer_pending;
    assert(0 == pthread_mutex_unlock(&g_observer_guard));
    return token;
}

static int c3task_observer_abort (uint32_t token)
{
    assert(0U != token);
    assert(0 == pthread_mutex_lock(&g_observer_guard));
    assert(0 < g_observer_pending);
    --g_observer_pending;
    assert(0 == pthread_mutex_unlock(&g_observer_guard));
    return 0;
}

static int c3task_observer_commit (uint32_t token, void * p_handle)
{
    assert((0U != token) && (NULL != p_handle));
    assert(0 == pthread_mutex_lock(&g_observer_guard));
    assert(0 < g_observer_pending);
    --g_observer_pending;
    ++g_observer_active;
    assert(0 == pthread_mutex_unlock(&g_observer_guard));
    return 0;
}

static int c3task_observer_cancel (void * p_handle)
{
    assert(NULL != p_handle);
    assert(0 == pthread_mutex_lock(&g_observer_guard));
    if (g_fail_observer_cancel > 0)
    {
        --g_fail_observer_cancel;
        assert(0 == pthread_mutex_unlock(&g_observer_guard));
        return -1;
    }
    assert(0 < g_observer_active);
    --g_observer_active;
    assert(0 == pthread_mutex_unlock(&g_observer_guard));
    return 0;
}

static int c3task_observer_release (void * p_handle)
{
    assert(NULL != p_handle);
    assert(0 == pthread_mutex_lock(&g_observer_guard));
    if (g_fail_observer_release > 0)
    {
        --g_fail_observer_release;
        assert(0 == pthread_mutex_unlock(&g_observer_guard));
        return -1;
    }
    assert(0 < g_observer_active);
    --g_observer_active;
    assert(0 == pthread_mutex_unlock(&g_observer_guard));
    return 0;
}

static pthread_mutex_t g_kernel_guard = PTHREAD_MUTEX_INITIALIZER;
static int             g_sem_exists[32];
static int             g_task_state[32];
static int             g_fail_sem_create;
static int             g_fail_sem_delete;
static int             g_fail_task_create;
static int             g_fail_task_start;
static int             g_fail_task_reference;
static int             g_fail_task_terminate;
static int             g_fail_task_delete;
static int             g_task_delete_calls;
static int             g_sem_delete_calls;
static int             g_task_create_calls;
static int             g_sem_create_calls;
static _Thread_local ID g_current_task;

static ID next_free (const int * p_values)
{
    for (ID id = 1; id < 32; ++id)
    {
        if (0 == p_values[id])
        {
            return id;
        }
    }
    return -1;
}

static ID tk_cre_sem (T_CSEM * p_config)
{
    (void)p_config;
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    ++g_sem_create_calls;
    if (g_fail_sem_create > 0)
    {
        --g_fail_sem_create;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    const ID id = next_free(g_sem_exists);
    assert(0 < id);
    g_sem_exists[id] = 1;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return id;
}

static ER tk_del_sem (ID id)
{
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    if (g_fail_sem_delete > 0)
    {
        --g_fail_sem_delete;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    if ((id <= 0) || (id >= 32) || (0 == g_sem_exists[id]))
    {
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return E_NOEXS;
    }
    g_sem_exists[id] = 0;
    ++g_sem_delete_calls;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static ID tk_cre_tsk (T_CTSK * p_config)
{
    assert((NULL != p_config->bufptr) && (0 < p_config->stksz));
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    ++g_task_create_calls;
    if (g_fail_task_create > 0)
    {
        --g_fail_task_create;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    const ID id = next_free(g_task_state);
    assert(0 < id);
    g_task_state[id] = TTS_DMT;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return id;
}

static ER tk_sta_tsk (ID id, INT parameter)
{
    (void)parameter;
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    assert((0 < id) && (id < 32) && (0 != g_task_state[id]));
    if (g_fail_task_start > 0)
    {
        --g_fail_task_start;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    g_task_state[id] = TTS_RDY;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static ER tk_ref_tsk (ID id, T_RTSK * p_state)
{
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    if (g_fail_task_reference > 0)
    {
        --g_fail_task_reference;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    if ((id <= 0) || (id >= 32) || (0 == g_task_state[id]))
    {
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return E_NOEXS;
    }
    p_state->tskstat = g_task_state[id];
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static ER tk_ter_tsk (ID id)
{
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    if (g_fail_task_terminate > 0)
    {
        --g_fail_task_terminate;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    assert((0 < id) && (id < 32) && (0 != g_task_state[id]));
    g_task_state[id] = TTS_DMT;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static ER tk_del_tsk (ID id)
{
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    if (g_fail_task_delete > 0)
    {
        --g_fail_task_delete;
        assert(0 == pthread_mutex_unlock(&g_kernel_guard));
        return -1;
    }
    assert((0 < id) && (id < 32) && (TTS_DMT == g_task_state[id]));
    g_task_state[id] = 0;
    ++g_task_delete_calls;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static ID tk_get_tid (void)
{
    return g_current_task;
}

static ER tk_get_otm (SYSTIM * p_time)
{
    static uint32_t now_ms;

    p_time->hi = 0;
    p_time->lo = now_ms++;
    return E_OK;
}

static ER tk_ext_tsk (void)
{
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    assert((0 < g_current_task) && (g_current_task < 32));
    g_task_state[g_current_task] = TTS_DMT;
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return E_OK;
}

static int shim_prio_to_tk (uint32_t priority)
{
    return (int)priority;
}

static void tm_printf (const UB * p_format, ...)
{
    (void)p_format;
}

@@FUNCTIONS@@

static void dummy_task (void * p_context)
{
    (void)p_context;
}

static int count_values (const int * p_values)
{
    int count = 0;
    assert(0 == pthread_mutex_lock(&g_kernel_guard));
    for (int index = 1; index < 32; ++index)
    {
        if (0 != p_values[index])
        {
            ++count;
        }
    }
    assert(0 == pthread_mutex_unlock(&g_kernel_guard));
    return count;
}

static int task_count (void)
{
    return count_values(g_task_state);
}

static int sem_count (void)
{
    return count_values(g_sem_exists);
}

static int shim_count (void)
{
    int count = 0;
    UINT saved;
    DI(saved);
    for (int index = 0; index < SHIM_TASK_MAX; ++index)
    {
        if (SHIM_TASK_FREE != s_task_states[index])
        {
            ++count;
        }
    }
    EI(saved);
    return count;
}

static ID task_id_for (TaskHandle_t handle)
{
    ID   id = 0;
    UINT saved;
    DI(saved);
    for (int index = 0; index < SHIM_TASK_MAX; ++index)
    {
        if (s_handles[index] == (uintptr_t)handle)
        {
            id = s_tasks[index].tsk_id;
            break;
        }
    }
    EI(saved);
    return id;
}

static TaskHandle_t create_task (uint32_t stack_bytes)
{
    TaskHandle_t handle = NULL;
    assert(pdTRUE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                    "radio",
                                                    stack_bytes,
                                                    NULL,
                                                    3U,
                                                    &handle,
                                                    0));
    assert(NULL != handle);
    return handle;
}

static void assert_clean (void)
{
    assert(0 == task_count());
    assert(0 == sem_count());
    assert(0 == stack_count());
    assert(0 == shim_count());
    assert(0 == g_observer_pending);
    assert(0 == g_observer_active);
}

static void reap_until_clean (void)
{
    for (int attempt = 0; attempt < 128; ++attempt)
    {
        const int result = shim_task_reap_one();
        if ((0 == result) && (0 == shim_count()))
        {
            assert_clean();
            return;
        }
    }
    assert(!"cleanup did not converge");
}

typedef struct
{
    TaskHandle_t handle;
} delete_context_t;

static void * delete_worker (void * p_context)
{
    delete_context_t * p_delete = p_context;
    __wrap_vTaskDelete(p_delete->handle);
    return NULL;
}

typedef struct
{
    uint32_t     stack_bytes;
    TaskHandle_t handle;
} create_context_t;

static void * create_worker (void * p_context)
{
    create_context_t * p_create = p_context;
    p_create->handle = create_task(p_create->stack_bytes);
    return NULL;
}

int main (void)
{
    TaskHandle_t untouched = (TaskHandle_t)(uintptr_t)99U;

    g_fail_sem_create = 1;
    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "sem-fail",
                                                     4096U,
                                                     NULL,
                                                     3U,
                                                     &untouched,
                                                     0));
    assert((TaskHandle_t)(uintptr_t)99U == untouched);
    assert_clean();

    g_fail_task_create = 1;
    g_fail_sem_delete  = 1;
    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "task-fail",
                                                     4096U,
                                                     NULL,
                                                     3U,
                                                     NULL,
                                                     0));
    assert((1 == sem_count()) && (1 == stack_count()));
    reap_until_clean();

    g_fail_task_start      = 1;
    g_fail_observer_cancel = 1;
    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "start-fail",
                                                     4096U,
                                                     NULL,
                                                     3U,
                                                     NULL,
                                                     0));
    assert((1 == task_count()) && (1 == g_observer_active));
    reap_until_clean();

    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "oversize",
                                                     6657U,
                                                     NULL,
                                                     3U,
                                                     NULL,
                                                     0));
    assert_clean();

    TaskHandle_t handles[4] = {create_task(2816U),
                               create_task(3584U),
                               create_task(4096U),
                               create_task(6656U)};
    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "exhausted",
                                                     1U,
                                                     NULL,
                                                     3U,
                                                     NULL,
                                                     0));
    for (int index = 0; index < 4; ++index)
    {
        __wrap_vTaskDelete(handles[index]);
    }
    reap_until_clean();

    TaskHandle_t handle = create_task(4096U);
    __wrap_vTaskDelete(handle);
    g_fail_task_delete = 1;
    assert(-1 == shim_task_reap_one());
    assert((1 == task_count()) && (1 == sem_count()) && (1 == stack_count()));
    reap_until_clean();

    handle = create_task(4096U);
    __wrap_vTaskDelete(handle);
    g_fail_sem_delete = 1;
    const int task_deletes_before = g_task_delete_calls;
    assert(-1 == shim_task_reap_one());
    assert(g_task_delete_calls == task_deletes_before + 1);
    reap_until_clean();
    assert(g_task_delete_calls == task_deletes_before + 1);

    handle = create_task(4096U);
    __wrap_vTaskDelete(handle);
    g_fail_stack_release = 1;
    const int sem_deletes_before = g_sem_delete_calls;
    assert(-1 == shim_task_reap_one());
    assert(g_sem_delete_calls == sem_deletes_before + 1);
    reap_until_clean();
    assert(g_sem_delete_calls == sem_deletes_before + 1);

    handle = create_task(4096U);
    __wrap_vTaskDelete(handle);
    g_fail_observer_release = 1;
    assert(-1 == shim_task_reap_one());
    assert((0 == task_count()) && (0 == sem_count()) && (0 == stack_count()));
    assert(1 == shim_count());
    reap_until_clean();

    handle         = create_task(4096U);
    g_current_task = task_id_for(handle);
    assert(0 < g_current_task);
    __wrap_vTaskDelete(NULL);
    reap_until_clean();
    g_current_task = 0;

    handle = create_task(4096U);
    delete_context_t delete_context = {.handle = handle};
    pthread_t first_delete;
    pthread_t second_delete;
    assert(0 == pthread_create(&first_delete,
                               NULL,
                               delete_worker,
                               &delete_context));
    assert(0 == pthread_create(&second_delete,
                               NULL,
                               delete_worker,
                               &delete_context));
    assert(0 == pthread_join(first_delete, NULL));
    assert(0 == pthread_join(second_delete, NULL));
    assert((1 == task_count()) && (1 == sem_count()) && (1 == stack_count()));
    reap_until_clean();

    TaskHandle_t stale_handle = create_task(4096U);
    __wrap_vTaskDelete(stale_handle);
    reap_until_clean();
    TaskHandle_t current_handle = create_task(4096U);
    assert(stale_handle != current_handle);
    __wrap_vTaskDelete(stale_handle);
    assert((1 == task_count()) && (1 == stack_count()));
    __wrap_vTaskDelete(current_handle);
    reap_until_clean();

    create_context_t create_contexts[4] = {{.stack_bytes = 2816U},
                                           {.stack_bytes = 3584U},
                                           {.stack_bytes = 4096U},
                                           {.stack_bytes = 6656U}};
    pthread_t creators[4];
    for (int index = 0; index < 4; ++index)
    {
        assert(0 == pthread_create(&creators[index],
                                   NULL,
                                   create_worker,
                                   &create_contexts[index]));
    }
    for (int index = 0; index < 4; ++index)
    {
        assert(0 == pthread_join(creators[index], NULL));
        assert(NULL != create_contexts[index].handle);
        for (int earlier = 0; earlier < index; ++earlier)
        {
            assert(create_contexts[earlier].handle !=
                   create_contexts[index].handle);
        }
        __wrap_vTaskDelete(create_contexts[index].handle);
    }
    reap_until_clean();

    for (int cycle = 0; cycle < 1000; ++cycle)
    {
        handle = create_task(4096U);
        __wrap_vTaskDelete(handle);
        reap_until_clean();
    }

    const int task_creates_before_seal = g_task_create_calls;
    const int sem_creates_before_seal  = g_sem_create_calls;
    g_pool_sealed = 1;
    assert(pdFALSE == __wrap_xTaskCreatePinnedToCore(dummy_task,
                                                     "sealed",
                                                     4096U,
                                                     NULL,
                                                     3U,
                                                     NULL,
                                                     0));
    assert(task_creates_before_seal == g_task_create_calls);
    assert(sem_creates_before_seal == g_sem_create_calls);
    assert_clean();

    puts("PASS: applied shim patch creation rollback, exhaustion, lifecycle retries,");
    puts("      post-seal zero-create rejection and 1000 reuse cycles under ASan/UBSan");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="buddy1_shim_actual_") as folder:
        directory = Path(folder)
        staged = directory / "components/freertos_shim/shim_task.c"
        staged.parent.mkdir(parents=True)
        shutil.copyfile(VENDOR_SHIM, staged)
        subprocess.run(
            ["git", "apply", "--check", str(PATCH)],
            cwd=directory,
            check=True,
        )
        subprocess.run(["git", "apply", str(PATCH)], cwd=directory, check=True)
        source = staged.read_text(encoding="utf-8")

        if re.search(r"\bheap_caps_(?:malloc|free)\s*\(", source):
            raise RuntimeError("Applied shim still contains dynamic stack calls")
        if not re.search(r"#define\s+SHIM_TASK_MAX\s+4\b", source):
            raise RuntimeError("Applied shim task bound differs")

        enum_match = re.search(
            r"typedef\s+enum\s*\{.*?\}\s*shim_task_state_t\s*;",
            source,
            re.DOTALL,
        )
        if enum_match is None:
            raise RuntimeError("Applied shim lifecycle state is missing")

        names = (
            "make_handle",
            "clear_task_entry",
            "alloc_task_entry",
            "find_index_by_tk_id",
            "find_index_by_handle",
            "release_unstarted_entry",
            "cleanup_time_ms",
            "defer_cleanup",
            "paint_stack",
            "__wrap_xTaskCreatePinnedToCore",
            "shim_task_reap_one",
            "__wrap_vTaskDelete",
        )
        functions = "\n\n".join(extract_function(source, name) for name in names)
        program = HARNESS.replace("@@STATE_ENUM@@", enum_match.group(0))
        program = program.replace("@@FUNCTIONS@@", functions)

        test_source = directory / "test_shim.c"
        binary = directory / "test_shim"
        test_source.write_text(program, encoding="utf-8")
        command = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-pthread",
            str(test_source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == "__main__":
    main()
