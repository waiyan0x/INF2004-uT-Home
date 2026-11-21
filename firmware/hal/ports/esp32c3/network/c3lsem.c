/**
 * @file c3lsem.c
 * @brief Reserve the station controller's lwIP semaphore at startup.
 */
#include "c3lsem.h"

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "freertos/semphr.h"
#include <stdbool.h>

static sys_sem_t         g_record  = {0};
static SemaphoreHandle_t gh_record = NULL;
static int               gh_owner  = 0;

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

