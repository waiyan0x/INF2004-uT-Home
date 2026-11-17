/**
 * @file touttest.c
 * @brief Count SDK paths separately from the actual fixed-buffer adapter.
 */
#include "touttest.h"
#include "c3tout.h"
#include <stdlib.h>
#include <string.h>

static uint32_t g_counts[5] = {0};

/** @brief Model setup/cleanup call order, without claiming a TLS handshake. */
int touttest_setup (touttest_context_t *      p_ssl,
                    const touttest_config_t * p_config)
{
    p_ssl->p_out_buf = NULL;
    if (1U == p_config->failure)
    {
        return -1;
    }
    if (5U == p_config->failure)
    {
        /* Neither a different shape nor an early output request is a record. */
        void * p_other = c3tout_alloc(2U, 16717U);
        c3tout_free(p_other);
        p_other = c3tout_alloc(1U, 16716U);
        c3tout_free(p_other);
        p_other = c3tout_alloc(1U, 4429U);
        c3tout_free(p_other);
    }
    p_ssl->p_in_buf = c3tout_alloc(1U, 16717U);
    if (NULL == p_ssl->p_in_buf)
    {
        return -1;
    }
    if (4U == p_config->failure)
    {
        c3tout_free(p_ssl->p_in_buf);
        p_ssl->p_in_buf = NULL;
        return -1;
    }
    p_ssl->p_out_buf = c3tout_alloc(1U, 4429U);
    if ((NULL == p_ssl->p_out_buf) || (2U == p_config->failure))
    {
        c3tout_free(p_ssl->p_in_buf);
        c3tout_free(p_ssl->p_out_buf);
        p_ssl->p_in_buf  = NULL;
        p_ssl->p_out_buf = NULL;
        return -1;
    }
    if (3U == p_config->failure)
    {
        /* Later same-sized objects must retain the ordinary SDK path. */
        void * p_other = c3tout_alloc(1U, 4429U);
        c3tout_wipe(p_other, 4429U);
        p_other = c3tout_alloc(1U, 16717U);
        c3tout_wipe(p_other, 16717U);
    }
    return 0;
}

/** @brief Model the SDK name setter; compiled SDK probes check its real code.
 */
int touttest_name (touttest_context_t * p_ssl, const char * p_hostname)
{
    ++g_counts[4];
    const size_t length = (NULL == p_hostname) ? 0U : strlen(p_hostname);
    if (255U < length)
    {
        return -135;
    }
    if (NULL != p_ssl->p_hostname)
    {
        c3tout_wipe(p_ssl->p_hostname, strlen(p_ssl->p_hostname));
    }
    p_ssl->p_hostname = NULL;
    if (NULL != p_hostname)
    {
        p_ssl->p_hostname = c3tout_alloc(1U, length + 1U);
        if (NULL == p_ssl->p_hostname)
        {
            return -141;
        }
        memcpy(p_ssl->p_hostname, p_hostname, length + 1U);
    }
    p_ssl->flags |= 1U;
    return 0;
}

/** @brief Count forwarding; this host-only SDK double owns ordinary heap. */
void * touttest_alloc (size_t count, size_t size)
{
    ++g_counts[0];
    return calloc(count, size);
}

/** @brief Forwarded SDK storage is separate from the adapter's fixed slots. */
void touttest_free (void * p_memory)
{
    ++g_counts[1];
    free(p_memory);
}

/** @brief Preserve explicit erasure for host verification of buffer release. */
void touttest_zeroize (void * p_memory, size_t size)
{
    ++g_counts[2];
    volatile unsigned char * p_bytes = p_memory;
    for (size_t index = 0U; index < size; ++index)
    {
        p_bytes[index] = 0U;
    }
}

/** @brief Mirror the SDK's wipe-then-free boundary for other storage. */
void touttest_wipe (void * p_memory, size_t size)
{
    ++g_counts[3];
    if (NULL != p_memory)
    {
        touttest_zeroize(p_memory, size);
    }
    touttest_free(p_memory);
}

/** @brief Expose deterministic forwarding/erasure counters to the host test. */
uint32_t touttest_count (uint32_t kind)
{
    return (kind < 5U) ? g_counts[kind] : UINT32_MAX;
}

/* End of file. */

