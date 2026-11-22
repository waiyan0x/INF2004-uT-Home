/**
 * @file c3lsem.c
 * @brief Reserve the station controller's lwIP semaphore at startup.
 */
#include "c3lsem.h"

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "freertos/semphr.h"
#include "pthread_key_registry.h"
#include "pthread_mutex_record.h"
#include <errno.h>
#include <stdbool.h>

/* The linked station registers C++ exception and Wi-Fi semaphore keys. */
#define C3LSEM_GENERIC_KEYS (2U)
static key_entry_t g_keys[C3LSEM_GENERIC_KEYS]  = {0};
static bool        gb_used[C3LSEM_GENERIC_KEYS] = {false};

#define C3LSEM_MUTEXES (5U)
static esp_pthread_mutex_t g_mutexes[C3LSEM_MUTEXES]     = {0};
static bool                gb_mutex_used[C3LSEM_MUTEXES] = {false};

static key_entry_t       g_socket_key = {0};
static sys_sem_t         g_record     = {0};
static SemaphoreHandle_t gh_record    = NULL;
static int               gh_owner     = 0;

static bool is_owner (void);

/** @brief The sole station startup caller runs before other socket users. */
int c3lsem_start (void)
{
    const int h_task = c3bport_task();
    if (h_task <= 0)
    {
        return -1;
    }
    if (NULL != gh_record)
    {
        return (h_task == gh_owner) ? 0 : -1;
    }
    gh_record = xSemaphoreCreateBinaryStatic(&g_record);
    if (NULL == gh_record)
    {
        return -1;
    }
    gh_owner = h_task;
    return 0;
}

/** @brief Register the single socket key before other tasks use lwIP. */
int c3lsem_key (pthread_key_t * p_key, c3lsem_destructor_t * p_destructor)
{
    const uint32_t saved = c3bport_enter();
    if (0U == g_socket_key.key)
    {
        const key_entry_t * const p_head = SLIST_FIRST(&s_keys);
        if ((NULL != p_head) && (UINT32_MAX == p_head->key))
        {
            c3bport_leave(saved);
            return EAGAIN;
        }
        g_socket_key.key        = (NULL == p_head) ? 1U : (p_head->key + 1U);
        g_socket_key.destructor = p_destructor;
        SLIST_INSERT_HEAD(&s_keys, &g_socket_key, next);
    }
    *p_key = g_socket_key.key;
    c3bport_leave(saved);
    return 0;
}

/** @brief Lease a typed registry record until native key deletion. */
void * c3lsem_reserve (void)
{
    key_entry_t *  p_key = NULL;
    const uint32_t saved = c3bport_enter();
    for (size_t index = 0U; index < C3LSEM_GENERIC_KEYS; ++index)
    {
        if (!gb_used[index])
        {
            gb_used[index] = true;
            p_key          = &g_keys[index];
            break;
        }
    }
    c3bport_leave(saved);
    return p_key;
}

/** @brief Native deletion calls this after unlinking, under its registry lock.
 */
void c3lsem_unlinked (const void * p_entry)
{
    if (&g_socket_key == p_entry)
    {
        g_socket_key.key = 0U;
    }
    else
    {
        for (size_t index = 0U; index < C3LSEM_GENERIC_KEYS; ++index)
        {
            if (&g_keys[index] == p_entry)
            {
                gb_used[index] = false;
                break;
            }
        }
    }
}

/** @brief Interrupt exclusion also covers the early C++ constructors. */
void * c3lsem_mutex_reserve (void)
{
    esp_pthread_mutex_t * p_record = NULL;
    const uint32_t        saved    = c3bport_enter();
    for (size_t index = 0U; index < C3LSEM_MUTEXES; ++index)
    {
        if (!gb_mutex_used[index])
        {
            gb_mutex_used[index] = true;
            p_record             = &g_mutexes[index];
            break;
        }
    }
    c3bport_leave(saved);
    return p_record;
}

/** @brief A slot becomes reusable only after its native owner is finished. */
void c3lsem_mutex_release (const void * p_record)
{
    const uint32_t saved = c3bport_enter();
    for (size_t index = 0U; index < C3LSEM_MUTEXES; ++index)
    {
        if (&g_mutexes[index] == p_record)
        {
            gb_mutex_used[index] = false;
            break;
        }
    }
    c3bport_leave(saved);
}

/** @brief Keep independent SDK tasks on their own per-thread records. */
sys_sem_t * c3lsem_get (void)
{
    return is_owner() ? &g_record : c3lsem_native_get();
}

/** @brief Initialization on the permanent owner preserves any live signal. */
sys_sem_t * c3lsem_init (void)
{
    return is_owner() ? &g_record : c3lsem_native_init();
}

/** @brief Drain a pending binary signal before the controller reuses it. */
void c3lsem_deinit (void)
{
    if (is_owner())
    {
        /* Both an empty semaphore and a successful take finish empty. */
        (void)xSemaphoreTake(gh_record, 0U);
    }
    else
    {
        c3lsem_native_deinit();
    }
}

/** @brief Native task identity stays constant through controller callbacks. */
static bool is_owner (void)
{
    return (gh_owner > 0) && (c3bport_task() == gh_owner);
}
#endif

/* End of file. */

