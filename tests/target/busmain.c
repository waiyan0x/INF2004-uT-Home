/**
 * @file busmain.c
 * @brief Run one bus fault scenario with fresh static adapter state.
 */
#include "busmain.h"

#include <inttypes.h>
#include "busfake.h"

/** @brief Accept only the bounded scenario numbers used by the host runner. */
int main (int argc, char * p_argv[])
{
    if (2 != argc)
    {
        return 1;
    }
    char *         p_end    = NULL;
    const intmax_t scenario = strtoimax(p_argv[1], &p_end, 10);
    if (('\0' != *p_end) || (!(((scenario >= 1L) && (scenario <= 13L)) ||
                               ((scenario >= 20L) && (scenario <= 27L)))))
    {
        return 1;
    }
    /* Only 1..13 and 20..27 reach this conversion, all representable as int. */
    busfake_verify((int)scenario);
    return 0;
}

/* End of file. */

