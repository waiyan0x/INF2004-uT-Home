/**
 * @file c3boot.c
 * @brief Reserve only the audited native startup needs, retaining a margin.
 */
#include "c3boot.h"

#include "esp_rom_sys.h"

#if UTHOME_CONFIGURATION_APP && UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#define C3BOOT_POOL_BYTES (11168U - C3BOOT_STACK_BYTES)
static uint64_t g_stack[C3BOOT_STACK_BYTES / sizeof(uint64_t)] = {0};
#else
#define C3BOOT_POOL_BYTES 12288U
#endif

/* Native allocator requires eight-byte alignment; workers own separate stacks.
 */
static uint64_t g_pool[C3BOOT_POOL_BYTES / sizeof(uint64_t)] = {0};

#if UTHOME_CONFIGURATION_APP && UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
/** @brief The kernel receives this stack only for its initial task. */
void * c3boot_stack (void)
{
    return g_stack;
}
#endif

/** @brief Hand one fixed pool to the original kernel, never an SDK heap. */
void c3boot_start (void)
{
    gp_c3boot_begin = g_pool;
    gp_c3boot_end   = &g_pool[C3BOOT_POOL_BYTES / sizeof(uint64_t)];
    esp_rom_printf("[static-start] native startup pool: %u bytes\n",
                   C3BOOT_POOL_BYTES);
    const int32_t result = c3boot_native_main();
    esp_rom_printf("[static-start] unexpected native return: %d\n", result);
    for (;;)
    {
        /* A failed kernel startup cannot safely retry partially live objects.
         */
    }
}

/* End of file. */

