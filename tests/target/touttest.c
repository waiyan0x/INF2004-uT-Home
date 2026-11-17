/**
 * @file touttest.c
 * @brief Count SDK paths separately from the actual fixed-buffer adapter.
 */
#include "touttest.h"
#include "c3tout.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error "Host checks require assertions."
#endif

static uint32_t g_counts[8] = {0};
static int state_init (touttest_context_t * p_ssl);
static void other (size_t count, size_t size);
static void other_states (void);

/** @brief Model setup/cleanup call order, without claiming a TLS handshake. */
int touttest_setup (touttest_context_t *      p_ssl,
                    const touttest_config_t * p_config)
{
    p_ssl->p_config  = p_config;
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
    if (6U == p_config->failure)
    {
        other_states();
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
    const int result = state_init(p_ssl);
    if (0 != result)
    {
        c3tout_free(p_ssl->p_in_buf);
        c3tout_free(p_ssl->p_out_buf);
        p_ssl->p_in_buf  = NULL;
        p_ssl->p_out_buf = NULL;
    }
    return result;
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

/** @brief Model the native start's init and first nonblocking handshake poll.
 */
int touttest_start (touttest_context_t * p_ssl)
{
    ++g_counts[5];
    const int result = state_init(p_ssl);
    if (0 != result)
    {
        return result;
    }
    p_ssl->state         = 0;
    p_ssl->renegotiation = 1;
    return -0x6900;
}

/** @brief The native same-unit call does not pass through the start wrapper. */
int touttest_renegotiate (touttest_context_t * p_ssl)
{
    ++g_counts[6];
    if ((NULL == p_ssl) || (NULL == p_ssl->p_config))
    {
        other_states();
        return -135;
    }
    if (1 == p_ssl->renegotiation)
    {
        other_states();
        return -0x6900;
    }
    if (16 != p_ssl->state)
    {
        other_states();
        return -135;
    }
    return touttest_start(p_ssl);
}

/** @brief Model outer-record promotion; native SDK probes cover inner cleanup.
 */
void touttest_promote (touttest_context_t * p_ssl)
{
    ++g_counts[7];
    c3tout_free(p_ssl->p_handshake);
    c3tout_free(p_ssl->p_transform);
    c3tout_free(p_ssl->p_session);
    p_ssl->p_handshake     = NULL;
    p_ssl->p_transform     = p_ssl->p_transform_new;
    p_ssl->p_session       = p_ssl->p_session_new;
    p_ssl->p_transform_new = NULL;
    p_ssl->p_session_new   = NULL;
    p_ssl->state           = 16;
    p_ssl->renegotiation   = 2;
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
    return (kind < 8U) ? g_counts[kind] : UINT32_MAX;
}

/** @brief Preserve native request order, skipped existing records and failures.
 */
static int state_init (touttest_context_t * p_ssl)
{
    const bool b_interleave =
        (NULL != p_ssl->p_config) && (6U == p_ssl->p_config->failure);
    if (b_interleave)
    {
        other(1U, sizeof(touttest_session_t));
        other(1U, sizeof(touttest_handshake_t));
        other(2U, sizeof(touttest_transform_t));
    }
    if (NULL == p_ssl->p_transform_new)
    {
        p_ssl->p_transform_new = c3tout_alloc(1U, sizeof(touttest_transform_t));
    }
    if (b_interleave)
    {
        other(1U, sizeof(touttest_transform_t));
        other(1U, sizeof(touttest_handshake_t));
    }
    if (NULL == p_ssl->p_session_new)
    {
        p_ssl->p_session_new = c3tout_alloc(1U, sizeof(touttest_session_t));
    }
    if (b_interleave)
    {
        other(1U, sizeof(touttest_transform_t));
        other(1U, sizeof(touttest_session_t));
    }
    if (NULL == p_ssl->p_handshake)
    {
        p_ssl->p_handshake = c3tout_alloc(1U, sizeof(touttest_handshake_t));
    }
    if ((NULL == p_ssl->p_transform_new) || (NULL == p_ssl->p_session_new) ||
        (NULL == p_ssl->p_handshake))
    {
        c3tout_free(p_ssl->p_transform_new);
        c3tout_free(p_ssl->p_session_new);
        c3tout_free(p_ssl->p_handshake);
        p_ssl->p_transform_new = NULL;
        p_ssl->p_session_new   = NULL;
        p_ssl->p_handshake     = NULL;
        return -141;
    }
    if (b_interleave)
    {
        other_states();
        touttest_promote(p_ssl);
        other_states();
    }
    return 0;
}

/** @brief An unrelated allocation must reach the counted SDK heap exactly once.
 */
static void other (size_t count, size_t size)
{
    const uint32_t before  = g_counts[0];
    void *         p_other = c3tout_alloc(count, size);
    assert(NULL != p_other);
    assert((before + 1U) == g_counts[0]);
    c3tout_wipe(p_other, count * size);
}

/** @brief Probe all equal-size request shapes outside their initialization
 * phase. */
static void other_states (void)
{
    other(1U, sizeof(touttest_transform_t));
    other(1U, sizeof(touttest_session_t));
    other(1U, sizeof(touttest_handshake_t));
}

/* End of file. */

