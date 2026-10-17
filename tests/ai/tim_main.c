/**
 * @file tim_main.c
 * @brief Host parity for exported inference using bounded unsigned vectors.
 */
#include "tim_main.h"

#include "timtree.h"

#include <inttypes.h>
#include <stdio.h>

/** @brief Read six uint32 values per test line and print the classification. */
int main (void)
{
    uint32_t features[TIMTREE_FEATURE_COUNT] = {0U};

    if ((-1 != timtree_predict(NULL, TIMTREE_FEATURE_COUNT)) ||
        (-1 != timtree_predict(features, 0U)))
    {
        return 1;
    }
    while (6 == scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                      " %" SCNu32,
                      &features[0],
                      &features[1],
                      &features[2],
                      &features[3],
                      &features[4],
                      &features[5]))
    {
        if (printf("%d\n", timtree_predict(features, TIMTREE_FEATURE_COUNT)) <
            0)
        {
            return 1;
        }
    }
    return 0;
}

/* End of file. */

