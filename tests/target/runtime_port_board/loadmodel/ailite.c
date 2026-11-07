/**
 * @file ailite.c
 * @brief Bounded protocol-tree template; exported data remains experimental.
 */
#include "ailite.h"

/* Experimental hardware model; deployment disabled. */
/* Model SHA256: */
/* 96f73ceff2e7943c4f1b23f3d333252d13bffef9c6eccb16ee7281c07d13bd58 */

/* Each row: feature plus one (zero for leaf), threshold, left, right, score. */
static const uint32_t g_nodes[][5] = {
    /* NODES_BEGIN */
    {11U, 1500U, 1U, 6U, 583U},
    {3U, 875U, 2U, 3U, 166U},
    {0U, 0U, 0U, 0U, 0U},
    {2U, 1500U, 4U, 5U, 500U},
    {0U, 0U, 0U, 0U, 1000U},
    {0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 1000U}
    /* NODES_END */
};

static const uint32_t g_domain[AILITE_FEATURE_COUNT][2] = {
    /* DOMAIN_BEGIN */
    {0U, 0U},
    {200U, 2000U},
    {500U, 1000U},
    {2U, 3U},
    {0U, 0U},
    {100000U, 100000U},
    {12U, 12U},
    {0U, 1U},
    {1U, 1U},
    {100000U, 400000U},
    {500U, 5000U},
    {1U, 1U},
    {1000000U, 4000000U},
    {1000U, 9091U},
    {0U, 1U},
    {0U, 256000U},
    {0U, 300U},
    {0U, 512U}
    /* DOMAIN_END */
};

/** @brief Visit at most five immutable tree nodes after checking all inputs. */
int ailite_predict (const uint32_t * p_features, size_t feature_count)
{
    uint32_t node_index = 0U;

    if ((NULL == p_features) || (AILITE_FEATURE_COUNT != feature_count))
    {
        return -1;
    }
    for (size_t feature = 0U; feature < AILITE_FEATURE_COUNT; ++feature)
    {
        if ((p_features[feature] < g_domain[feature][0]) ||
            (p_features[feature] > g_domain[feature][1]))
        {
            return -1;
        }
    }
    /* Export validation proves bounded child indices and depth <= 4. */
    for (uint32_t depth = 0U; depth < 5U; ++depth)
    {
        const uint32_t * p_node = g_nodes[node_index];

        if (0U == p_node[0])
        {
            return (p_node[4] >= 900U) ? 1 : 0;
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

