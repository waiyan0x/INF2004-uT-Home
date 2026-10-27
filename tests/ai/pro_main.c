/**
 * @file pro_main.c
 * @brief Check eighteen-input exported inference with bounded host vectors.
 */
#include "pro_main.h"

#include "protree.h"

#include <inttypes.h>
#include <stdio.h>

/** @brief Read complete unsigned vectors and print one classification each. */
int main (void)
{
    uint32_t features[PROTREE_FEATURE_COUNT] = {0U};

    if ((-1 != protree_predict(NULL, PROTREE_FEATURE_COUNT)) ||
        (-1 != protree_predict(features, 0U)) ||
        (-1 != protree_predict(features, PROTREE_FEATURE_COUNT + 1U)))
    {
        return 1;
    }
    while (1 == scanf("%" SCNu32, &features[0]))
    {
        for (size_t index = 1U; index < PROTREE_FEATURE_COUNT; ++index)
        {
            if (1 != scanf("%" SCNu32, &features[index]))
            {
                return 1;
            }
        }
        if (printf("%d\n", protree_predict(features, PROTREE_FEATURE_COUNT)) <
            0)
        {
            return 1;
        }
    }
    return 0;
}

/* End of file. */

