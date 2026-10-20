/**
 * @file timtree.c
 * @brief Non-deployable model template; exporter inserts validated integer
 * data.
 */
#include "timtree.h"

/* Each row: feature plus one (zero for leaf), threshold, left, right, score. */
static const uint32_t g_nodes[][5] = {
    /* NODES_BEGIN */
    {0U, 0U, 0U, 0U, 0U}
    /* NODES_END */
};

static const uint32_t g_domain[TIMTREE_FEATURE_COUNT][2] = {
    /* DOMAIN_BEGIN */
    {0U, 0U},
    {0U, 0U},
    {0U, 0U},
    {0U, 0U},
    {0U, 0U},
    {0U, 0U}
    /* DOMAIN_END */
};

/** @brief Evaluate the bounded, immutable depth-four tree without recursion. */
int timtree_predict (const uint32_t * p_features, size_t feature_count)
{
    uint32_t node_index = 0U;

    if ((NULL == p_features) || (TIMTREE_FEATURE_COUNT != feature_count))
    {
        return -1;
    }
    for (size_t feature = 0U; feature < TIMTREE_FEATURE_COUNT; feature++)
    {
        if ((p_features[feature] < g_domain[feature][0]) ||
            (p_features[feature] > g_domain[feature][1]))
        {
            return -1;
        }
    }
    /* Export validation proves every child index is in range and depth <= 4. */
    for (uint32_t depth = 0U; depth < 5U; depth++)
    {
        const uint32_t * p_node = g_nodes[node_index];

        if (0U == p_node[0])
        {
            if (p_node[4] >= 900U)
            {
                return 1;
            }
            return 0;
        }
        if (p_features[p_node[0] - 1U] <= p_node[1])
        {
            node_index = p_node[2];
        }
        else
        {
            node_index = p_node[3];
        }
    }
    return -1;
}

/* End of file. */

