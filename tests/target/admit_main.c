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
#include "heaptrap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t   g_input[8193] = {0};
static cfgplan_t g_plan        = {0};
static int parse_only (const uint8_t * p_data, size_t size);

/** @brief Parse independent documents in one process to catch stale state. */
int main (int argc, char ** pp_argv)
{
    assert(0 == cfgjson_init());
    assert(-1 == cfgjson_init());
    assert(NULL == cfgjson_open(NULL, 0U));
    if ((2 == argc) && (0 == strcmp(pp_argv[1], "heap-probe")))
    {
        /* Confirm interception without deliberately aborting a process. */
        cJSON_InitHooks(NULL);
        cfgjson_value_t * p_probe = cJSON_Parse("{}");
        cJSON_Delete(p_probe);
        printf("heap probe intercepted: %zu\n", heaptrap_count());
        return ((NULL == p_probe) && (1U == heaptrap_count())) ? 0 : 4;
    }
    if (argc < 3)
    {
        return 2;
    }
    if (0 == strcmp(pp_argv[1], "heap-violation"))
    {
        /* Verify normal failure reporting for an injected allocator defect. */
        cJSON_InitHooks(NULL);
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
        const int result = (0 == strcmp(pp_argv[1], "parser"))
                               ? parse_only(g_input, size)
                               : cfgplan_read(g_input, size, mask, &g_plan);
        if (0U != heaptrap_count())
        {
            fprintf(
                stderr, "Unexpected heap use: %zu calls\n", heaptrap_count());
            return 3;
        }
        assert(cfgjson_peak() <= UTHOME_JSON_BYTES);
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

/** @brief Check pool ownership independently of profile semantic admission. */
static int parse_only (const uint8_t * p_data, size_t size)
{
    memset(&g_plan, 0, sizeof(g_plan));
    const cfgjson_value_t * p_tree = cfgjson_open(p_data, size);
    const int               result = (NULL != p_tree) ? 0 : -1;
    if (NULL != p_tree)
    {
        /* A rejected concurrent open must not invalidate the borrowed tree. */
        assert(NULL == cfgjson_open(p_data, size));
        assert(0 != cJSON_IsObject(p_tree));
    }
    cfgjson_close();
    cfgjson_close();
    return result;
}

/* End of file. */

