/**
 * @file c3tout.c
 * @brief Retain MQTT TLS record buffers through SDK retries/cleanup.
 */
/* Buffer lengths and setup state belong to the pinned SDK's private ABI. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "c3tout.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "ssl_misc.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH) ||                             \
    (MBEDTLS_SSL_IN_BUFFER_LEN <= MBEDTLS_SSL_OUT_BUFFER_LEN)
#error "Fixed TLS buffers require distinct sizes without buffer resizing."
#endif

#define C3TOUT_SLOTS 1U

typedef union
{
    uint64_t      alignment;
    unsigned char bytes[MBEDTLS_SSL_OUT_BUFFER_LEN];
} c3tout_buffer_t;

typedef union
{
    uint64_t      alignment;
    unsigned char bytes[MBEDTLS_SSL_IN_BUFFER_LEN];
} c3tout_input_t;

static c3tout_buffer_t       g_buffers[C3TOUT_SLOTS]     = {0};
static bool                  gb_used[C3TOUT_SLOTS]       = {false};
static c3tout_input_t        g_inputs[C3TOUT_SLOTS]      = {0};
static bool                  gb_input_used[C3TOUT_SLOTS] = {false};
static mbedtls_ssl_context * gp_setup                    = NULL;
static bool release (void * p_memory);

/** @brief Scope allocation selection to this synchronous SDK setup call. */
int c3tout_setup (mbedtls_ssl_context *      p_ssl,
                  const mbedtls_ssl_config * p_config)
{
    gp_setup         = p_ssl;
    const int result = c3tout_native_setup(p_ssl, p_config);
    gp_setup         = NULL;
    return result;
}

/** @brief Select setup record buffers, never a same-sized later object.
 */
void * c3tout_alloc (size_t count, size_t size)
{
    if ((NULL != gp_setup) && (1U == count) &&
        (MBEDTLS_SSL_IN_BUFFER_LEN == size) &&
        (NULL == gp_setup->MBEDTLS_PRIVATE(in_buf)) &&
        (NULL == gp_setup->MBEDTLS_PRIVATE(out_buf)))
    {
        for (size_t index = 0U; index < C3TOUT_SLOTS; ++index)
        {
            if (!gb_input_used[index])
            {
                memset(g_inputs[index].bytes, 0, sizeof(g_inputs[index].bytes));
                gb_input_used[index] = true;
                return g_inputs[index].bytes;
            }
        }
        return NULL;
    }
    if ((NULL != gp_setup) && (1U == count) &&
        (MBEDTLS_SSL_OUT_BUFFER_LEN == size) &&
        (NULL != gp_setup->MBEDTLS_PRIVATE(in_buf)) &&
        (NULL == gp_setup->MBEDTLS_PRIVATE(out_buf)))
    {
        for (size_t index = 0U; index < C3TOUT_SLOTS; ++index)
        {
            if (!gb_used[index])
            {
                memset(
                    g_buffers[index].bytes, 0, sizeof(g_buffers[index].bytes));
                gb_used[index] = true;
                return g_buffers[index].bytes;
            }
        }
        return NULL;
    }
    return mbedtls_calloc(count, size);
}

/** @brief SDK setup failure uses ordinary free on both selected buffers. */
void c3tout_free (void * p_memory)
{
    if (!release(p_memory))
    {
        mbedtls_free(p_memory);
    }
}

/** @brief SDK normal cleanup wipes and releases through this same boundary.
 */
void c3tout_wipe (void * p_memory, size_t size)
{
    if (!release(p_memory))
    {
        mbedtls_zeroize_and_free(p_memory, size);
    }
}

/** @brief Match exact buffer identities; erase secrets before publishing free.
 */
static bool release (void * p_memory)
{
    for (size_t index = 0U; index < C3TOUT_SLOTS; ++index)
    {
        if (p_memory == g_inputs[index].bytes)
        {
            mbedtls_platform_zeroize(g_inputs[index].bytes,
                                     sizeof(g_inputs[index].bytes));
            gb_input_used[index] = false;
            return true;
        }
        if (p_memory == g_buffers[index].bytes)
        {
            mbedtls_platform_zeroize(g_buffers[index].bytes,
                                     sizeof(g_buffers[index].bytes));
            gb_used[index] = false;
            return true;
        }
    }
    return false;
}
#endif

/* End of file. */

