/**
 * @file ailite.h
 * @brief Experimental protocol classifier with eighteen ordered inputs.
 */
#ifndef UTHOME_AILITE_H
#define UTHOME_AILITE_H

#include <stddef.h>
#include <stdint.h>

#define AILITE_FEATURE_COUNT (18U)
#define AILITE_DEPLOYABLE    (0U)

/**
 * @brief Classify a protocol-tree 0.2.0 vector in ai/workload.py order.
 * @param p_features Borrowed array of eighteen unsigned configuration facts.
 * @param feature_count Number of elements; must equal AILITE_FEATURE_COUNT.
 * @return -1 for invalid/out-of-domain, 0 for predicted fail, 1 for pass.
 * @note No allocation, hardware access, writable globals or recursion. Caller
 * must independently check catalogue/build context, profile identity and full
 * admission. A score is experimental; this API never authorizes Apply.
 */
int ailite_predict (const uint32_t * p_features, size_t feature_count);

#endif

/* End of file. */

