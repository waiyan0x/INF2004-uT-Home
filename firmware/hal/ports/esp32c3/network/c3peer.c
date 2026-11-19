/**
 * @file c3peer.c
 * @brief Share typed storage between USB views and MQTT peer intermediates.
 */
#include "c3peer.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "mbedtls/platform.h"
#include "c3bport.h"
#include <string.h>
#include "mbedtls/x509_crt.h"

#define C3PEER_NODES 4U

typedef union
{
    cfgjson_value_t  views[UTHOME_JSON_NODES];
    mbedtls_x509_crt certs[C3PEER_NODES];
} c3peer_store_t;

/* Reject SDK changes that would grow the original 4 KiB workspace. */
enum
{
    C3PEER_SIZE_CHECK = 1 / (sizeof(c3peer_store_t) <= UTHOME_JSON_BYTES)
};

static c3peer_store_t g_store  = {0};
static bool           gb_views = false;
static size_t         g_live   = 0U;
static int            gh_owner = 0;

/** @brief Lend all view records to the serialized configuration parser. */
cfgjson_value_t * c3peer_views (void)
{
    if (gb_views || (0U != g_live))
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
    if (gb_views)
    {
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }
    /* In the no-copy parser an occupied chain's first allocation in
     * x509_crt.c is its additional node. The SDK owns linking and rollback. */
    if ((NULL == p_chain) || (0 == p_chain->version) ||
        (C3PEER_NODES == g_live))
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
#endif

/* End of file. */

