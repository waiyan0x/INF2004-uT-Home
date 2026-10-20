/**
 * @file timtree.h
 * @brief Experimental integer timing classifier; no deployment authority.
 */
#ifndef UTHOME_TIMTREE_H
#define UTHOME_TIMTREE_H

#include <stddef.h>
#include <stdint.h>

#define TIMTREE_FEATURE_COUNT (6U)
#define TIMTREE_DEPLOYABLE    (0U)

/**
 * @brief Classify six ordered features: kind, period, goal, tasks, source,
 * timeout.
 * @param p_features Borrowed array in the versioned timing-tree feature order.
 * @param feature_count Number of elements; must equal TIMTREE_FEATURE_COUNT.
 * @return -1 for invalid/out-of-domain input, 0 for failure, 1 for candidate
 * pass.
 * @note Caller must also check model origin, catalogue/build and full
 * admission. No hardware access, allocation, shared state or configuration
 * change.
 */
int timtree_predict (const uint32_t * p_features, size_t feature_count);

#endif

/* End of file. */

