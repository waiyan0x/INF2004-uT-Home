/**
 * @file c3tout.c
 * @brief Retain MQTT TLS records through SDK retries and cleanup.
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

#if !defined(MBEDTLS_SSL_PROTO_TLS1_2) || !defined(MBEDTLS_SSL_CLI_C) ||       \
    !defined(MBEDTLS_SSL_RENEGOTIATION) ||                                     \
    defined(MBEDTLS_SSL_PROTO_TLS1_3) || defined(MBEDTLS_SSL_SRV_C) ||         \
    defined(MBEDTLS_SSL_PROTO_DTLS) ||                                         \
    defined(MBEDTLS_SSL_KEEP_PEER_CERTIFICATE) ||                              \
    defined(MBEDTLS_ECP_RESTARTABLE) ||                                        \
    defined(MBEDTLS_SSL_ECP_RESTARTABLE_ENABLED) ||                            \
    (32 != MBEDTLS_SSL_PEER_CERT_DIGEST_DFL_LEN)
#error "Fixed TLS state requires the reviewed TLS 1.2 client profile."
#endif

#define C3TOUT_SLOTS      1U
#define C3TOUT_NAME_BYTES 254U
#define C3TOUT_STATES     2U

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

static c3tout_buffer_t              g_buffers[C3TOUT_SLOTS]          = {0};
static bool                         gb_used[C3TOUT_SLOTS]            = {false};
static c3tout_input_t               g_inputs[C3TOUT_SLOTS]           = {0};
static bool                         gb_input_used[C3TOUT_SLOTS]      = {false};
static mbedtls_ssl_context *        gp_setup                         = NULL;
static char                         g_hostname[C3TOUT_NAME_BYTES]    = {0};
static bool                         gb_name_used                     = false;
static mbedtls_ssl_context *        gp_name                          = NULL;
static size_t                       g_name_size                      = 0U;
static mbedtls_ssl_transform        g_transforms[C3TOUT_STATES]      = {0};
static bool                         gb_transform_used[C3TOUT_STATES] = {false};
static mbedtls_ssl_session          g_sessions[C3TOUT_STATES]        = {0};
static bool                         gb_session_used[C3TOUT_STATES]   = {false};
static mbedtls_ssl_handshake_params g_handshakes[C3TOUT_SLOTS]       = {0};
static bool                         gb_handshake_used[C3TOUT_SLOTS]  = {false};
static mbedtls_ssl_context *        gp_state                         = NULL;
static uint32_t                     g_state_pending                  = 0U;
static mbedtls_x509_crt             g_certificate                    = {0};
static bool                         gb_certificate_used              = false;
static uint8_t g_peer_digest[MBEDTLS_SSL_PEER_CERT_DIGEST_DFL_LEN]   = {0};
static bool    gb_digest_used                                        = false;
static mbedtls_ssl_context * gp_certificate                          = NULL;
static uint32_t              g_cert_phase                            = 0U;
static void begin_state (mbedtls_ssl_context * p_ssl);
static bool select_certificate (size_t count, size_t size, void ** pp_memory);
static bool select_state (size_t count, size_t size, void ** pp_memory);
static bool release_state (void * p_memory);
static bool release (void * p_memory);

/** @brief Scope allocation selection to this synchronous SDK setup call. */
int c3tout_setup (mbedtls_ssl_context *      p_ssl,
                  const mbedtls_ssl_config * p_config)
{
    gp_setup = p_ssl;
    begin_state(p_ssl);
    const int result = c3tout_native_setup(p_ssl, p_config);
    gp_setup         = NULL;
    begin_state(NULL);
    return result;
}

/** @brief HelloRequest entry always initializes negotiating records. */
int c3tout_start (mbedtls_ssl_context * p_ssl)
{
    begin_state(p_ssl);
    const int result = c3tout_native_start(p_ssl);
    begin_state(NULL);
    return result;
}

/** @brief New renegotiation initializes; in-progress retries do not. */
int c3tout_renegotiate (mbedtls_ssl_context * p_ssl)
{
    if ((NULL != p_ssl) && (NULL != p_ssl->MBEDTLS_PRIVATE(conf)) &&
        (MBEDTLS_SSL_HANDSHAKE_OVER == p_ssl->MBEDTLS_PRIVATE(state)) &&
        (MBEDTLS_SSL_RENEGOTIATION_IN_PROGRESS !=
         p_ssl->MBEDTLS_PRIVATE(renego_status)))
    {
        begin_state(p_ssl);
    }
    const int result = c3tout_native_renegotiate(p_ssl);
    begin_state(NULL);
    return result;
}

/** @brief Scope native parsing without changing verification or failure state.
 */
int c3tout_certificate (mbedtls_ssl_context * p_ssl)
{
    gp_certificate   = p_ssl;
    g_cert_phase     = 1U;
    const int result = c3tout_native_certificate(p_ssl);
    gp_certificate   = NULL;
    g_cert_phase     = 0U;
    return result;
}

/** @brief Scope the SDK setter while preserving its copy, flags and cleanup. */
int c3tout_set_name (mbedtls_ssl_context * p_ssl, const char * p_hostname)
{
    size_t length = 0U;
    if (NULL != p_hostname)
    {
        while ((length < sizeof(g_hostname)) && ('\0' != p_hostname[length]))
        {
            ++length;
        }
        if (sizeof(g_hostname) == length)
        {
            return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
        }
    }
    gp_name          = p_ssl;
    g_name_size      = (NULL == p_hostname) ? 0U : (length + 1U);
    const int result = c3tout_native_name(p_ssl, p_hostname);
    gp_name          = NULL;
    g_name_size      = 0U;
    return result;
}

/** @brief Select scoped storage; unrelated allocations retain the SDK path.
 */
void * c3tout_alloc (size_t count, size_t size)
{
    void * p_state = NULL;
    if ((select_state(count, size, &p_state)) ||
        (select_certificate(count, size, &p_state)))
    {
        return p_state;
    }
    if ((NULL != gp_name) && (1U == count) && (0U != g_name_size) &&
        (g_name_size == size) && (NULL == gp_name->MBEDTLS_PRIVATE(hostname)))
    {
        if (gb_name_used)
        {
            return NULL;
        }
        memset(g_hostname, 0, sizeof(g_hostname));
        gb_name_used = true;
        return g_hostname;
    }
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

/** @brief SDK failure cleanup releases selected records without heap free. */
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
    if (p_memory == &g_certificate)
    {
        mbedtls_platform_zeroize(&g_certificate, sizeof(g_certificate));
        gb_certificate_used = false;
        if ((NULL != gp_certificate) && (2U == g_cert_phase))
        {
            g_cert_phase = 3U;
        }
        return true;
    }
    if (p_memory == g_peer_digest)
    {
        mbedtls_platform_zeroize(g_peer_digest, sizeof(g_peer_digest));
        gb_digest_used = false;
        return true;
    }
    if (release_state(p_memory))
    {
        return true;
    }
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
    if (p_memory == g_hostname)
    {
        mbedtls_platform_zeroize(g_hostname, sizeof(g_hostname));
        gb_name_used = false;
        return true;
    }
    return false;
}

/** @brief Select the peer-chain head, then its digest only after head release.
 * The pinned parser frees the prior matching digest before requesting a new
 * one. Consume each request even on exhaustion; later requests stay SDK-owned.
 */
static bool select_certificate (size_t count, size_t size, void ** pp_memory)
{
    if ((NULL == gp_certificate) || (1U != count))
    {
        return false;
    }
    if ((1U == g_cert_phase) && (sizeof(g_certificate) == size))
    {
        g_cert_phase = 0U;
        if (!gb_certificate_used)
        {
            memset(&g_certificate, 0, sizeof(g_certificate));
            gb_certificate_used = true;
            g_cert_phase        = 2U;
            *pp_memory          = &g_certificate;
        }
        return true;
    }
    if ((3U == g_cert_phase) && (sizeof(g_peer_digest) == size) &&
        (NULL != gp_certificate->MBEDTLS_PRIVATE(session_negotiate)) &&
        (NULL == gp_certificate->MBEDTLS_PRIVATE(session_negotiate)
                     ->MBEDTLS_PRIVATE(peer_cert_digest)))
    {
        g_cert_phase = 0U;
        if (!gb_digest_used)
        {
            memset(g_peer_digest, 0, sizeof(g_peer_digest));
            gb_digest_used = true;
            *pp_memory     = g_peer_digest;
        }
        return true;
    }
    return false;
}

/** @brief Snapshot missing outer records; existing ones are reused by the SDK.
 */
static void begin_state (mbedtls_ssl_context * p_ssl)
{
    gp_state        = p_ssl;
    g_state_pending = 0U;
    if (NULL != p_ssl)
    {
        if (NULL == p_ssl->MBEDTLS_PRIVATE(transform_negotiate))
        {
            g_state_pending |= 1U;
        }
        if (NULL == p_ssl->MBEDTLS_PRIVATE(session_negotiate))
        {
            g_state_pending |= 2U;
        }
        if (NULL == p_ssl->MBEDTLS_PRIVATE(handshake))
        {
            g_state_pending |= 4U;
        }
    }
}

/** @brief Follow the pinned SDK's uninterrupted transform/session/handshake
 * initialization sequence. Consume an attempt even when its store is full;
 * routing ends before checksum setup, callbacks or handshake progression.
 */
static bool select_state (size_t count, size_t size, void ** pp_memory)
{
    if ((NULL == gp_state) || (1U != count) ||
        (NULL == gp_state->MBEDTLS_PRIVATE(in_buf)) ||
        (NULL == gp_state->MBEDTLS_PRIVATE(out_buf)))
    {
        return false;
    }
    if ((0U != (g_state_pending & 1U)) && (sizeof(g_transforms[0]) == size) &&
        (NULL == gp_state->MBEDTLS_PRIVATE(transform_negotiate)))
    {
        g_state_pending &= ~1U;
        for (size_t index = 0U; index < C3TOUT_STATES; ++index)
        {
            if (!gb_transform_used[index])
            {
                memset(&g_transforms[index], 0, sizeof(g_transforms[index]));
                gb_transform_used[index] = true;
                *pp_memory               = &g_transforms[index];
                break;
            }
        }
        return true;
    }
    if ((2U == (g_state_pending & 3U)) && (sizeof(g_sessions[0]) == size) &&
        (NULL == gp_state->MBEDTLS_PRIVATE(session_negotiate)))
    {
        g_state_pending &= ~2U;
        for (size_t index = 0U; index < C3TOUT_STATES; ++index)
        {
            if (!gb_session_used[index])
            {
                memset(&g_sessions[index], 0, sizeof(g_sessions[index]));
                gb_session_used[index] = true;
                *pp_memory             = &g_sessions[index];
                break;
            }
        }
        return true;
    }
    if ((4U == g_state_pending) && (sizeof(g_handshakes[0]) == size) &&
        (NULL == gp_state->MBEDTLS_PRIVATE(handshake)))
    {
        g_state_pending = 0U;
        if (!gb_handshake_used[0])
        {
            memset(&g_handshakes[0], 0, sizeof(g_handshakes[0]));
            gb_handshake_used[0] = true;
            *pp_memory           = &g_handshakes[0];
        }
        return true;
    }
    return false;
}

/** @brief Release exact typed identities after the SDK has freed interiors. */
static bool release_state (void * p_memory)
{
    for (size_t index = 0U; index < C3TOUT_STATES; ++index)
    {
        if (p_memory == &g_transforms[index])
        {
            mbedtls_platform_zeroize(&g_transforms[index],
                                     sizeof(g_transforms[index]));
            gb_transform_used[index] = false;
            return true;
        }
        if (p_memory == &g_sessions[index])
        {
            mbedtls_platform_zeroize(&g_sessions[index],
                                     sizeof(g_sessions[index]));
            gb_session_used[index] = false;
            return true;
        }
    }
    if (p_memory == &g_handshakes[0])
    {
        mbedtls_platform_zeroize(&g_handshakes[0], sizeof(g_handshakes[0]));
        gb_handshake_used[0] = false;
        return true;
    }
    return false;
}
#endif

/* End of file. */

