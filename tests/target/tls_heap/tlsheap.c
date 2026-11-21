/**
 * @file tlsheap.c
 * @brief Count native TLS allocations without allocating observer storage.
 */
#include "tlsheap.h"
#include "c3bport.h"
#include "esp_mem.h"
#include <stdint.h>

#define TLSHEAP_BLOCKS  64U
#define TLSHEAP_OUTSIDE (MBEDTLS_SSL_HANDSHAKE_OVER + 1)

typedef struct
{
    void * p_memory;
    size_t size;
} tlsheap_block_t;

typedef struct
{
    uint32_t requests;
    size_t   requested;
    size_t   peak;
    size_t   largest;
} tlsheap_stage_t;

/* The linker binds the native names to the unwrapped SDK implementations. */
void * tlsheap_native_alloc (size_t count, size_t size);
void tlsheap_native_free (void * p_memory);
int tlsheap_native_step (mbedtls_ssl_context * p_ssl);
int tlsheap_native_connect (const char *          p_name,
                            int                   length,
                            int                   port,
                            const esp_tls_cfg_t * p_config,
                            esp_tls_t *           p_tls);

/* The debugger reads these while halted. Only the MQTT owner updates stages. */
static volatile tlsheap_stage_t g_stages[TLSHEAP_OUTSIDE + 1];
static tlsheap_block_t          g_blocks[TLSHEAP_BLOCKS];
static int                      gh_owner   = 0;
static int                      g_stage    = TLSHEAP_OUTSIDE;
static volatile size_t          g_live     = 0U;
static volatile size_t          g_released = 0U;
static volatile uint32_t        g_frees    = 0U;
static volatile uint32_t        g_errors   = 0U;

/** @brief Identify the persistent MQTT worker at its first connection. */
int tlsheap_connect (const char *          p_name,
                     int                   length,
                     int                   port,
                     const esp_tls_cfg_t * p_config,
                     esp_tls_t *           p_tls)
{
    const int      h_task = c3bport_task();
    const uint32_t saved  = c3bport_enter();
    if ((0 >= h_task) || ((0 != gh_owner) && (h_task != gh_owner)))
    {
        ++g_errors;
    }
    gh_owner = h_task;
    c3bport_leave(saved);
    return tlsheap_native_connect(p_name, length, port, p_config, p_tls);
}

/** @brief Attribute native requests to the active handshake state. */
int tlsheap_step (mbedtls_ssl_context * p_ssl)
{
    const int state    = p_ssl->MBEDTLS_PRIVATE(state);
    const int previous = g_stage;
    int       result;
    if (gh_owner != c3bport_task())
    {
        return tlsheap_native_step(p_ssl);
    }
    if ((0 <= state) && (TLSHEAP_OUTSIDE > state))
    {
        g_stage = state;
    }
    else
    {
        ++g_errors;
    }
    result  = tlsheap_native_step(p_ssl);
    g_stage = previous;
    return result;
}

/** @brief Measure requested bytes for successful owner allocations. */
void * tlsheap_alloc (size_t count, size_t size)
{
    void *         p_memory = tlsheap_native_alloc(count, size);
    const int      h_task   = c3bport_task();
    const uint32_t saved    = c3bport_enter();
    if ((0 < gh_owner) && (gh_owner == h_task))
    {
        volatile tlsheap_stage_t * p_stage = &g_stages[g_stage];
        size_t                     index   = 0U;
        ++p_stage->requests;
        while ((TLSHEAP_BLOCKS > index) && (NULL != g_blocks[index].p_memory))
        {
            ++index;
        }
        if ((NULL == p_memory) || (TLSHEAP_BLOCKS == index) ||
            ((0U != size) && ((SIZE_MAX / size) < count)))
        {
            ++g_errors;
        }
        else
        {
            const size_t bytes       = count * size;
            g_blocks[index].p_memory = p_memory;
            g_blocks[index].size     = bytes;
            g_live += bytes;
            p_stage->requested += bytes;
            if (p_stage->peak < g_live)
            {
                p_stage->peak = g_live;
            }
            if (p_stage->largest < bytes)
            {
                p_stage->largest = bytes;
            }
        }
    }
    c3bport_leave(saved);
    return p_memory;
}

/** @brief Retire the block before the SDK can reuse its address. */
void tlsheap_free (void * p_memory)
{
    if (NULL != p_memory)
    {
        const uint32_t saved = c3bport_enter();
        size_t         index = 0U;
        while ((TLSHEAP_BLOCKS > index) &&
               (p_memory != g_blocks[index].p_memory))
        {
            ++index;
        }
        if (TLSHEAP_BLOCKS > index)
        {
            g_live -= g_blocks[index].size;
            g_released += g_blocks[index].size;
            g_blocks[index].p_memory = NULL;
            ++g_frees;
        }
        c3bport_leave(saved);
    }
    tlsheap_native_free(p_memory);
}

/* End of file. */

