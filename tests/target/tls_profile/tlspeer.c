/**
 * @file tlspeer.c
 * @brief Real pinned TLS/crypto with copied host-only ciphertext queues.
 */
#include "tlspeer.h"

#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_ciphersuites.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#ifdef NDEBUG
#error "TLS profile fixture requires assertions"
#endif

/** Host transport storage, not an ESP reservation or a TLS record buffer. */
#define TLSPEER_QUEUE_SIZE 32768U

/** A copied byte queue used only at the TLS ciphertext callback boundary. */
typedef struct
{
    unsigned char data[TLSPEER_QUEUE_SIZE];
    size_t        used;
} tlspeer_queue_t;

static mbedtls_ssl_context g_peer;
static mbedtls_ssl_config  g_config;
static mbedtls_x509_crt    g_certificate;
static tlspeer_queue_t     g_incoming;
static tlspeer_queue_t     g_outgoing;
/* Both suites are enabled in the selected ESP image; host crypto stays
 * software. */
static const int g_ciphers[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
                                MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
                                0};

static int
send_bytes (void * p_context, const unsigned char * p_data, size_t size);
static int
receive_bytes (void * p_context, unsigned char * p_data, size_t capacity);
static int configure_peer (const char * p_ca);

int tlspeer_start (const char * p_ca, const char * p_name)
{
    int status;

    mbedtls_ssl_init(&g_peer);
    mbedtls_ssl_config_init(&g_config);
    mbedtls_x509_crt_init(&g_certificate);
    g_incoming.used = 0U;
    g_outgoing.used = 0U;
    if (PSA_SUCCESS != psa_crypto_init())
    {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    status = configure_peer(p_ca);
    if (0 == status)
    {
        status = mbedtls_ssl_setup(&g_peer, &g_config);
    }
    if (0 == status)
    {
        status = mbedtls_ssl_set_hostname(&g_peer, p_name);
    }
    if (0 == status)
    {
        mbedtls_ssl_set_bio(&g_peer, NULL, send_bytes, receive_bytes, NULL);
    }
    return status;
}

void tlspeer_close (void)
{
    mbedtls_ssl_free(&g_peer);
    mbedtls_ssl_config_free(&g_config);
    mbedtls_x509_crt_free(&g_certificate);
    memset(&g_incoming, 0, sizeof(g_incoming));
    memset(&g_outgoing, 0, sizeof(g_outgoing));
}

int tlspeer_handshake (void)
{
    return mbedtls_ssl_handshake(&g_peer);
}

int tlspeer_read (unsigned char * p_data, size_t capacity)
{
    return mbedtls_ssl_read(&g_peer, p_data, capacity);
}

int tlspeer_write (const unsigned char * p_data, size_t size)
{
    return mbedtls_ssl_write(&g_peer, p_data, size);
}

bool tlspeer_feed (const unsigned char * p_data, size_t size)
{
    if (size > (sizeof(g_incoming.data) - g_incoming.used))
    {
        return false;
    }
    memcpy(&g_incoming.data[g_incoming.used], p_data, size);
    g_incoming.used += size;
    return true;
}

size_t tlspeer_drain (unsigned char * p_data, size_t capacity)
{
    size_t size = (capacity < g_outgoing.used) ? capacity : g_outgoing.used;

    memcpy(p_data, g_outgoing.data, size);
    g_outgoing.used -= size;
    memmove(g_outgoing.data, &g_outgoing.data[size], g_outgoing.used);
    return size;
}

uint32_t tlspeer_verify (void)
{
    return mbedtls_ssl_get_verify_result(&g_peer);
}

const char * tlspeer_protocol (void)
{
    return mbedtls_ssl_get_version(&g_peer);
}

const char * tlspeer_cipher (void)
{
    return mbedtls_ssl_get_ciphersuite(&g_peer);
}

size_t tlspeer_limit (bool b_input)
{
    return b_input ? MBEDTLS_SSL_IN_CONTENT_LEN : MBEDTLS_SSL_OUT_CONTENT_LEN;
}

void tlspeer_error (int status, char * p_text, size_t capacity)
{
    mbedtls_strerror(status, p_text, capacity);
}

static int
send_bytes (void * p_context, const unsigned char * p_data, size_t size)
{
    /* The serialized singleton queues require no borrowed callback context. */
    (void)p_context;
    assert(size <= INT_MAX);
    if (size > (sizeof(g_outgoing.data) - g_outgoing.used))
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    memcpy(&g_outgoing.data[g_outgoing.used], p_data, size);
    g_outgoing.used += size;
    /* The queue bound and assertion keep the SDK's int result representable. */
    return (int)size;
}

static int
receive_bytes (void * p_context, unsigned char * p_data, size_t capacity)
{
    size_t size = (capacity < g_incoming.used) ? capacity : g_incoming.used;

    /* The serialized singleton queues require no borrowed callback context. */
    (void)p_context;
    if (0U == size)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    memcpy(p_data, g_incoming.data, size);
    g_incoming.used -= size;
    memmove(g_incoming.data, &g_incoming.data[size], g_incoming.used);
    /* size cannot exceed the fixed queue's 32768 bytes or the host INT_MAX. */
    return (int)size;
}

static int configure_peer (const char * p_ca)
{
    int status = mbedtls_ssl_config_defaults(&g_config,
                                             MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT);

    if (0 != status)
    {
        return status;
    }
    mbedtls_ssl_conf_min_tls_version(&g_config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ciphersuites(&g_config, g_ciphers);
    mbedtls_ssl_conf_authmode(&g_config, MBEDTLS_SSL_VERIFY_REQUIRED);
    status = mbedtls_x509_crt_parse_file(&g_certificate, p_ca);
    if (0 != status)
    {
        return status;
    }
    mbedtls_ssl_conf_ca_chain(&g_config, &g_certificate, NULL);
    return status;
}

/* End of file. */

