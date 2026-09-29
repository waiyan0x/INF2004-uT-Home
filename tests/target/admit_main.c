/**
 * @file admit_main.c
 * @brief Exercise exactly bounded file input and reusable parser ownership.
 */
#ifdef NDEBUG
#error "Admission tests require assertions."
#endif
#include "admit_main.h"
#include "cfgplan.h"
#include "cfgjson.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t   g_input[8193] = {0};
static cfgplan_t g_plan        = {0};

/** @brief Parse independent documents in one process to catch stale state. */
int main (int argc, char ** pp_argv)
{
    assert(0 == cfgjson_init());
    assert(-1 == cfgjson_init());
    assert(NULL == cfgjson_open(NULL, 0U));
    if (argc < 3)
    {
        return 2;
    }
    const uint32_t mask = 31U;
    for (int index = 2; index < argc; ++index)
    {
        FILE * p_file = fopen(pp_argv[index], "rb");
        assert(NULL != p_file);
        const size_t size = fread(g_input, 1U, sizeof(g_input), p_file);
        assert(0 == ferror(p_file));
        assert(0 == fclose(p_file));
        memset(&g_plan, 0xA5, sizeof(g_plan));
        const int result = cfgplan_read(g_input, size, mask, &g_plan);
        assert(cfgjson_peak() <= UTHOME_JSON_HEAP);
        if (0 != result)
        {
            const cfgplan_t zero = {0};
            assert(0 == memcmp(&g_plan, &zero, sizeof(zero)));
        }
        printf("%d %zu %u %u %zu\n",
               result,
               g_plan.device_count,
               g_plan.active_tasks,
               g_plan.stack_bytes,
               cfgjson_peak());
    }
    return 0;
}

/* End of file. */

