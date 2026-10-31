/**
 * @file c3boot.c
 * @brief Reserve only the audited native startup needs, retaining a margin.
 */
#include "c3boot.h"

#include "esp_rom_sys.h"

#define C3BOOT_POOL_BYTES 12288U

/* Native allocator requires eight-byte alignment; workers own separate stacks.
 */
static uint64_t g_pool[C3BOOT_POOL_BYTES / sizeof(uint64_t)] = {0};

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

