/**
 * @file c3peer.c
 * @brief Share typed storage between USB views and MQTT peer intermediates.
 */
#include "c3peer.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "mbedtls/platform.h"
#include "mbedtls/private/ecp.h"
#include "c3bport.h"
#include "c3tout.h"
#include <string.h>
#include "mbedtls/platform_util.h"
#include "tf_psa_crypto_common.h"

/* The table must be local to one synchronous multiplication. */
#if (MBEDTLS_ECP_FIXED_POINT_OPTIM != 0) ||                                    \
    defined(MBEDTLS_ECP_RESTARTABLE) || (MBEDTLS_ECP_WINDOW_SIZE != 2)
#error "Review certificate point-table ownership for this EC configuration."
#endif

#define C3PEER_POINTS         2U
#define C3PEER_NODES          4U
#define C3PEER_SLOTS          32U
#define C3PEER_WORDS          24U
#define C3PEER_P256_BITS      256U
#define C3PEER_INVERSE_FACTOR 5U

typedef struct
{
    uint32_t                 words[C3PEER_SLOTS][C3PEER_WORDS];
    uint32_t                 used;
    const mbedtls_x509_crt * p_leaf;
    /* The SDK uses five 256-bit vectors for one P-256 modular inverse. */
    uint32_t inverse[C3PEER_INVERSE_FACTOR * (C3PEER_P256_BITS / 32U)];
    bool     b_inverse_used;
    mbedtls_ecp_keypair key;
    bool                b_key_used;
    bool                b_verify;
    bool                b_limbs;
    /* SEC 1 uncompressed point: one format byte and two 256-bit coordinates. */
    uint8_t           public_data[1U + (2U * (C3PEER_P256_BITS / 8U))];
    bool              b_import;
    bool              b_data_used;
    mbedtls_ecp_point points[C3PEER_POINTS];
    bool              b_points_used;
} c3peer_math_t;

typedef union
{
    cfgjson_value_t  views[UTHOME_JSON_NODES];
    mbedtls_x509_crt certs[C3PEER_NODES];
    c3peer_math_t    math;
} c3peer_store_t;

/* Reject SDK changes that would grow the original 4 KiB workspace. */
enum
{
    /* The other x509.c allocator constructs a distinct SDK name type. */
    C3PEER_NAME_CHECK =
        1 / (sizeof(mbedtls_x509_name) != sizeof(mbedtls_asn1_sequence)),
    C3PEER_SIZE_CHECK = 1 / (sizeof(c3peer_store_t) <= UTHOME_JSON_BYTES)
};

static c3peer_store_t g_store   = {0};
static bool           gb_views  = false;
static uint8_t        g_live    = 0U;
static int            gh_owner  = 0;
static int            gh_crypto = 0;

static bool borrow_crypto (bool b_verify);
static void return_crypto (void);
static bool owns_crypto (void);
static bool release_limb (void * p_memory);
static bool release_data (void * p_memory);

/** @brief Lend all view records to the serialized configuration parser. */
cfgjson_value_t * c3peer_views (void)
{
    if (gb_views || (0U != g_live) || (0 != gh_crypto))
    {
        return NULL;
    }
    gb_views = true;
    return g_store.views;
}

/** @brief Release a view tree after its final consumer returns. */
void c3peer_close (void)
{
    gb_views = false;
}

/** @brief Supply the first four intermediate nodes to the native DER parser. */
int c3peer_der (struct mbedtls_x509_crt * p_chain,
                const unsigned char *     p_data,
                size_t                    size)
{
    if (gb_views || (0 != gh_crypto))
    {
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }
    /* In the no-copy parser an occupied chain's first allocation in
     * x509_crt.c is its additional node. The SDK owns linking and rollback. */
    if ((NULL != p_chain) && (0 == p_chain->version))
    {
        return c3peer_leaf(p_chain, p_data, size);
    }
    if ((NULL == p_chain) || (C3PEER_NODES == g_live))
    {
        return mbedtls_x509_crt_parse_der_nocopy(p_chain, p_data, size);
    }
    const int      h_task = c3bport_task();
    const uint32_t saved  = c3bport_enter();
    gh_owner              = h_task;
    c3bport_leave(saved);
    const int result = mbedtls_x509_crt_parse_der_nocopy(p_chain, p_data, size);
    const uint32_t done = c3bport_enter();
    gh_owner            = 0;
    c3bport_leave(done);
    return result;
}

/** @brief Parse a first DER certificate with a temporary keypair if available.
 */
int c3peer_leaf (mbedtls_x509_crt * p_chain,
                 const uint8_t *    p_data,
                 size_t             size)
{
    const bool b_lend =
        (NULL != p_chain) && (0 == p_chain->version) && (borrow_crypto(false));
    if (b_lend)
    {
        g_store.math.p_leaf = p_chain;
    }
    const int result = mbedtls_x509_crt_parse_der_nocopy(p_chain, p_data, size);
    if (b_lend)
    {
        return_crypto();
    }
    return result;
}

/** @brief Keep additional names and independent parses on the SDK path. */
void * c3peer_name_alloc (size_t count, size_t size)
{
    if ((1U == count) && (sizeof(mbedtls_asn1_sequence) == size) &&
        (owns_crypto()) && (!g_store.math.b_verify))
    {
        void * p_node = c3tout_name_node(g_store.math.p_leaf);
        if (NULL != p_node)
        {
            return p_node;
        }
    }
    return mbedtls_calloc(count, size);
}

/** @brief Serve only the scoped peer parser's next typed node request. */
void * c3peer_alloc (size_t count, size_t size)
{
    if ((1U == count) && (sizeof(mbedtls_x509_crt) == size))
    {
        const int      h_task     = c3bport_task();
        const uint32_t saved      = c3bport_enter();
        const bool     b_selected = (0 < h_task) && (h_task == gh_owner);
        if (b_selected)
        {
            gh_owner = 0;
        }
        c3bport_leave(saved);
        if (b_selected)
        {
            mbedtls_x509_crt * p_next = &g_store.certs[g_live];
            ++g_live;
            memset(p_next, 0, sizeof(*p_next));
            return p_next;
        }
    }
    return mbedtls_calloc(count, size);
}

/** @brief Return fixed nodes after native cleanup and forward all other frees.
 */
void c3peer_free (void * p_memory)
{
    for (size_t index = 0U; index < C3PEER_NODES; ++index)
    {
        if (p_memory == &g_store.certs[index])
        {
            --g_live;
            return;
        }
    }
    mbedtls_free(p_memory);
}

/** @brief Lend unused USB storage during synchronous demo-chain verification.
 */
int c3peer_verify (mbedtls_x509_crt *               p_chain,
                   mbedtls_x509_crt *               p_ca,
                   mbedtls_x509_crl *               p_crl,
                   const mbedtls_x509_crt_profile * p_profile,
                   const char *                     p_name,
                   uint32_t *                       p_flags,
                   c3peer_check_t *                 p_check,
                   void *                           p_context,
                   mbedtls_x509_crt_restart_ctx *   p_restart)
{
    /* Other chains, callbacks and restartable calls retain SDK allocation. */
    const bool b_lend =
        (NULL == p_restart) && (NULL == p_check) && (NULL == p_crl) &&
        (NULL != p_chain) && (NULL == p_chain->next) &&
        (MBEDTLS_PK_SIGALG_ECDSA == p_chain->MBEDTLS_PRIVATE(sig_pk)) &&
        (NULL != p_ca) && (NULL == p_ca->next) &&
        (PSA_ECC_FAMILY_SECP_R1 == p_ca->pk.MBEDTLS_PRIVATE(ec_family)) &&
        (C3PEER_P256_BITS == mbedtls_pk_get_bitlen(&p_ca->pk)) &&
        (borrow_crypto(true));
    const int result = mbedtls_x509_crt_verify_restartable(p_chain,
                                                           p_ca,
                                                           p_crl,
                                                           p_profile,
                                                           p_name,
                                                           p_flags,
                                                           p_check,
                                                           p_context,
                                                           p_restart);
    if (b_lend)
    {
        return_crypto();
    }
    return result;
}

/** @brief Select only the transient uncompressed P-256 public-key import. */
psa_status_t c3peer_import (const psa_key_attributes_t * p_attributes,
                            const uint8_t *              p_data,
                            size_t                       size,
                            mbedtls_svc_key_id_t *       ph_key)
{
    if ((sizeof(g_store.math.public_data) != size) || (NULL == p_data) ||
        (0x04U != p_data[0]) ||
        (PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1) !=
         psa_get_key_type(p_attributes)) ||
        (PSA_KEY_LIFETIME_VOLATILE != psa_get_key_lifetime(p_attributes)) ||
        (!owns_crypto()))
    {
        return psa_import_key(p_attributes, p_data, size, ph_key);
    }
    /* The SDK releases the decoded key and its coordinates before return. */
    g_store.math.b_limbs  = true;
    g_store.math.b_import = true;
    const psa_status_t result =
        psa_import_key(p_attributes, p_data, size, ph_key);
    g_store.math.b_import = false;
    g_store.math.b_limbs  = g_store.math.b_verify;
    return result;
}

/** @brief Consume the selected import's single PSA slot-buffer request. */
void * c3peer_psa_alloc (size_t count, size_t size)
{
    if ((1U != count) || (sizeof(g_store.math.public_data) != size) ||
        (!owns_crypto()) || (!g_store.math.b_import))
    {
        return c3tout_prf_alloc(count, size);
    }
    g_store.math.b_import = false;
    if (g_store.math.b_data_used)
    {
        return NULL;
    }
    g_store.math.b_data_used = true;
    memset(g_store.math.public_data, 0, sizeof(g_store.math.public_data));
    return g_store.math.public_data;
}

/** @brief Preserve native PSA and fixed PRF cleanup for other buffers. */
void c3peer_psa_free (void * p_memory)
{
    if (!release_data(p_memory))
    {
        c3tout_free(p_memory);
    }
}

/** @brief Clear the entire fixed copy on PSA's zeroizing release. */
void c3peer_psa_wipe (void * p_memory, size_t size)
{
    if (!release_data(p_memory))
    {
        c3tout_wipe(p_memory, size);
    }
}

/** @brief Release only the exact address issued to the selected import. */
static bool release_data (void * p_memory)
{
    if (g_store.math.public_data != p_memory)
    {
        return false;
    }
    mbedtls_platform_zeroize(p_memory, sizeof(g_store.math.public_data));
    g_store.math.b_data_used = false;
    return true;
}

/** @brief Supply the synchronous certificate operation's temporary EC key. */
void * c3peer_key (size_t count, size_t size)
{
    if ((1U != count) || (sizeof(g_store.math.key) != size) || (!owns_crypto()))
    {
        return mbedtls_calloc(count, size);
    }
    if (g_store.math.b_key_used)
    {
        return NULL;
    }
    g_store.math.b_key_used = true;
    memset(&g_store.math.key, 0, sizeof(g_store.math.key));
    return &g_store.math.key;
}

/** @brief The SDK releases the key's inner allocations before the record. */
void c3peer_key_free (void * p_memory)
{
    if (p_memory == &g_store.math.key)
    {
        mbedtls_platform_zeroize(p_memory, sizeof(g_store.math.key));
        g_store.math.b_key_used = false;
        return;
    }
    mbedtls_free(p_memory);
}

/** @brief Lend the synchronous verifier its two-point multiplication table. */
void * c3peer_points (size_t count, size_t size)
{
    if ((C3PEER_POINTS != count) || (sizeof(mbedtls_ecp_point) != size) ||
        (!owns_crypto()) || (!g_store.math.b_verify))
    {
        return mbedtls_calloc(count, size);
    }
    if (g_store.math.b_points_used)
    {
        return NULL;
    }
    g_store.math.b_points_used = true;
    memset(g_store.math.points, 0, sizeof(g_store.math.points));
    return g_store.math.points;
}

/** @brief Release the table after the SDK frees each point's coordinates. */
void c3peer_points_free (void * p_memory)
{
    if (p_memory == g_store.math.points)
    {
        mbedtls_platform_zeroize(p_memory, sizeof(g_store.math.points));
        g_store.math.b_points_used = false;
        return;
    }
    mbedtls_free(p_memory);
}

/** @brief Lend integer slots during verification or a scoped key import. */
void * c3peer_limbs (size_t count, size_t size)
{
    if ((!owns_crypto()) || (!g_store.math.b_limbs))
    {
        return mbedtls_calloc(count, size);
    }
    if ((g_store.math.b_verify) && ((C3PEER_P256_BITS / 8U) == count) &&
        (C3PEER_INVERSE_FACTOR == size))
    {
        if (g_store.math.b_inverse_used)
        {
            return NULL;
        }
        g_store.math.b_inverse_used = true;
        memset(g_store.math.inverse, 0, sizeof(g_store.math.inverse));
        return g_store.math.inverse;
    }
    if (sizeof(uint32_t) != size)
    {
        return mbedtls_calloc(count, size);
    }
    if ((0U == count) || (C3PEER_WORDS < count))
    {
        return NULL;
    }
    for (size_t index = 0U; index < C3PEER_SLOTS; ++index)
    {
        const uint32_t bit = UINT32_C(1) << index;
        if (0U == (g_store.math.used & bit))
        {
            g_store.math.used |= bit;
            memset(g_store.math.words[index],
                   0,
                   sizeof(g_store.math.words[index]));
            return g_store.math.words[index];
        }
    }
    return NULL;
}

/** @brief Release the SDK's plain-free temporary or an owned slot. */
void c3peer_drop (void * p_memory)
{
    if (!release_limb(p_memory))
    {
        mbedtls_free(p_memory);
    }
}

/** @brief Preserve native clearing for all SDK-owned integer buffers. */
void c3peer_wipe (void * p_memory, size_t size)
{
    if (!release_limb(p_memory))
    {
        mbedtls_zeroize_and_free(p_memory, size);
    }
}

/** @brief USB, peer-node and crypto leases share the serialized MQTT/USB owner.
 */
static bool borrow_crypto (bool b_verify)
{
    const int      h_task = c3bport_task();
    const uint32_t saved  = c3bport_enter();
    const bool     b_available =
        (0 < h_task) && (!gb_views) && (0U == g_live) && (0 == gh_crypto);
    if (b_available)
    {
        g_store.math.b_import    = false;
        g_store.math.b_data_used = false;
        g_store.math.b_key_used  = false;
        g_store.math.b_verify    = b_verify;
        g_store.math.b_limbs     = b_verify;
        g_store.math.used        = 0U;
        if (b_verify)
        {
            g_store.math.b_points_used  = false;
            g_store.math.b_inverse_used = false;
        }
        gh_crypto = h_task;
    }
    c3bport_leave(saved);
    return b_available;
}

/** @brief End the lease only after the native parser or verifier returns. */
static void return_crypto (void)
{
    const uint32_t saved = c3bport_enter();
    gh_crypto            = 0;
    c3bport_leave(saved);
}

/** @brief Check the native task before touching the shared union. */
static bool owns_crypto (void)
{
    const int      h_task  = c3bport_task();
    const uint32_t saved   = c3bport_enter();
    const bool     b_owned = (0 < h_task) && (h_task == gh_crypto);
    c3bport_leave(saved);
    return b_owned;
}

/** @brief Wipe an exact slot before its next allocation. */
static bool release_limb (void * p_memory)
{
    if ((NULL != p_memory) && (owns_crypto()) && (g_store.math.b_limbs))
    {
        if (p_memory == g_store.math.inverse)
        {
            mbedtls_platform_zeroize(p_memory, sizeof(g_store.math.inverse));
            g_store.math.b_inverse_used = false;
            return true;
        }
        for (size_t index = 0U; index < C3PEER_SLOTS; ++index)
        {
            if (p_memory == g_store.math.words[index])
            {
                mbedtls_platform_zeroize(p_memory,
                                         sizeof(g_store.math.words[index]));
                g_store.math.used &= ~(UINT32_C(1) << index);
                return true;
            }
        }
    }
    return false;
}
#endif

/* End of file. */

