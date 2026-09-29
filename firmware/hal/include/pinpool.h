/**
 * @file pinpool.h
 * @brief Reserve pin sets for distinct retained owners without partial claims.
 */
#ifndef UTHOME_PINPOOL_H
#define UTHOME_PINPOOL_H
#include <stdint.h>

typedef struct
{
    const void * p_owners[32];
    uint32_t     allowed;
} pinpool_t;

/* A serialized lifecycle owner changes claims only while affected users stop.
 * Zero the pool and set allowed once. Owner identities must outlive claims. */
int pinpool_claim (pinpool_t * p_pool, uint32_t mask, const void * p_owner);
int pinpool_release (pinpool_t * p_pool, uint32_t mask, const void * p_owner);
#endif
/* End of file. */

