/**
 * @file touttest.h
 * @brief Host SDK-call double; no encryption or network is simulated.
 */
#ifndef UTHOME_TOUTTEST_H
#define UTHOME_TOUTTEST_H
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    unsigned char * p_in_buf;
    unsigned char * p_out_buf;
} touttest_context_t;

typedef struct
{
    uint32_t failure;
} touttest_config_t;

int touttest_setup (touttest_context_t *      p_ssl,
                    const touttest_config_t * p_config);
void * touttest_alloc (size_t count, size_t size);
void touttest_free (void * p_memory);
void touttest_zeroize (void * p_memory, size_t size);
void touttest_wipe (void * p_memory, size_t size);
uint32_t touttest_count (uint32_t kind);
#endif

/* End of file. */

