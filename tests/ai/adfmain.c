/**
 * @file adfmain.c
 * @brief Admit caller JSON before deriving a trusted protocol feature vector.
 */
#include "adfmain.h"

#include "adfeat.h"
#include "cfgjson.h"
#include <stdbool.h>

static cfgplan_t g_plan     = {0};
static bool      gb_started = false;

/** @brief Use the production fixed parser; tests serialize each request. */
int adfmain_run (uint8_t const * p_input,
                 size_t          size,
                 size_t          target,
                 uint32_t        goal_ms,
                 uint32_t *      p_features)
{
    if (!gb_started)
    {
        if (0 != cfgjson_init())
        {
            return -3;
        }
        gb_started = true;
    }
    if (0 != cfgplan_read(p_input, size, 31U, &g_plan))
    {
        return -2;
    }
    return adfeat_build(&g_plan, target, goal_ms, p_features);
}

/* End of file. */

