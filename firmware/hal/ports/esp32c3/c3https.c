/**
 * @file c3https.c
 * @brief Nonblocking lwIP sockets and pinned ESP-TLS server sessions.
 */
#include "c3https.h"
#if UTHOME_NETWORK_WIFI
#include "esp_tls.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef enum
{
    C3HTTPS_FREE = 0,
    C3HTTPS_HANDSHAKE,
    C3HTTPS_RECEIVE,
    C3HTTPS_READY,
    C3HTTPS_BODY
} c3https_state_t;

typedef struct
{
    nethttp_t       http;
    esp_tls_t *     p_tls;
    uint64_t        since_ms;
    int             socket;
    c3https_state_t state;
} c3https_client_t;

static c3https_client_t     g_clients[2]   = {0};
static c3https_config_t     g_config       = {0};
static esp_tls_cfg_server_t g_tls_config   = {0};
static uint8_t              g_reply[512]   = {0};
static uint8_t              g_receive[128] = {0};
static size_t               g_reply_size   = 0U;
static size_t               g_sent         = 0U;
static int                  g_writer       = -1;
static int                  g_uploader     = -1;
static int                  g_listener     = -1;
static uint64_t             g_last_ms      = 0U;
static uint64_t             g_listen_ms    = 0U;
static bool                 gb_started     = false;
static bool                 gb_attempted   = false;
static int listen_socket (void);
static void accept_client (uint64_t now_ms);
static void close_client (size_t index);
static void client_step (size_t index, uint64_t now_ms);
static bool retry (int result);
static void receive_bytes (size_t index, size_t count, uint64_t now_ms);
static int config_reply (size_t index, const netread_info_t * p_info);
static int state_reply (const cfgmux_t * p_mux);
static int result_reply (const cfgmux_t * p_mux,
                         uint32_t         request_id,
                         const uint32_t * p_fields,
                         int              status);
static int error_reply (int status);

static int response (size_t                 index,
                     const netread_info_t * p_info,
                     netread_capture_t *    p_capture,
                     void *                 p_context);

/** @brief Validate deployment inputs before touching socket or TLS state. */
int c3https_start (const c3https_config_t * p_config)
{
    nethttp_t check = {0};
    if ((NULL == p_config) || (gb_attempted) ||
        (NULL == p_config->p_certificate) || (NULL == p_config->p_key) ||
        (0U == p_config->port) || (2U > p_config->certificate_size) ||
        (8192U < p_config->certificate_size) || (2U > p_config->key_size) ||
        (8192U < p_config->key_size) ||
        (0U != p_config->p_certificate[p_config->certificate_size - 1U]) ||
        (0U != p_config->p_key[p_config->key_size - 1U]) ||
        (0 !=
         nethttp_init(&check, p_config->p_token, p_config->token_size, 0U)))
    {
        return -1;
    }
    /* The parser's temporary credential copy is not retained on this stack. */
    memset(&check, 0, sizeof(check));
    gb_attempted                  = true;
    g_config                      = *p_config;
    g_tls_config.servercert_buf   = p_config->p_certificate;
    g_tls_config.servercert_bytes = (unsigned int)p_config->certificate_size;
    g_tls_config.serverkey_buf    = p_config->p_key;
    g_tls_config.serverkey_bytes  = (unsigned int)p_config->key_size;
    g_tls_config.tls_handshake_timeout_ms = 10000U;
    gb_started                            = true;
    return 0;
}

/** @brief Pump transport work; mutations use only the existing controller. */
int c3https_step (uint64_t               now_ms,
                  bool                   b_online,
                  const netread_info_t * p_info,
                  netread_capture_t *    p_capture,
                  void *                 p_context)
{
    int result = 0;
    if ((!gb_started) || (NULL == p_info))
    {
        return -1;
    }
    if ((now_ms < g_last_ms) || (!b_online))
    {
        c3https_stop();
        g_last_ms = now_ms;
        return b_online ? -1 : 0;
    }
    g_last_ms = now_ms;
    if ((0 > g_listener) && (now_ms >= g_listen_ms))
    {
        g_listener = listen_socket();
        g_listen_ms =
            ((UINT64_MAX - now_ms) >= 1000U) ? now_ms + 1000U : UINT64_MAX;
        if (0 > g_listener)
        {
            return -1;
        }
    }
    if (0 <= g_listener)
    {
        accept_client(now_ms);
    }
    for (size_t index = 0U; index < 2U; ++index)
    {
        client_step(index, now_ms);
        if ((C3HTTPS_READY == g_clients[index].state) && (0 > g_writer))
        {
            const int replied = response(index, p_info, p_capture, p_context);
            if (0 > replied)
            {
                close_client(index);
                result = -1;
            }
            else if (0 < replied)
            {
                /* The caller refreshes metadata before further observations. */
                return 1;
            }
            else
            {
                /* Read-only response is staged without changing the plan. */
            }
        }
    }
    return result;
}

/** @brief Drop transport sessions only; configuration and samples are
 * untouched. */
void c3https_stop (void)
{
    for (size_t index = 0U; index < 2U; ++index)
    {
        close_client(index);
    }
    if (0 <= g_listener)
    {
        /* close failure is not retried on a possibly recycled descriptor. */
        (void)close(g_listener);
        g_listener = -1;
    }
    g_listen_ms = 0U;
}

/** @brief Create a nonblocking IPv4 listener, with no plaintext fallback. */
static int listen_socket (void)
{
    struct sockaddr_in address   = {0};
    const int          reuse     = 1;
    const int          socket_id = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (0 > socket_id)
    {
        return -1;
    }
    address.sin_family      = AF_INET;
    address.sin_port        = htons(g_config.port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((0 !=
         setsockopt(
             socket_id, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) ||
        (0 > fcntl(socket_id, F_SETFL, O_NONBLOCK)) ||
        (0 != bind(socket_id, (struct sockaddr *)&address, sizeof(address))) ||
        (0 != listen(socket_id, 2)))
    {
        /* Descriptor is no longer used even when SDK close reports failure. */
        (void)close(socket_id);
        return -1;
    }
    return socket_id;
}

/** @brief Reject exhausted connections without allocating another TLS session.
 */
static void accept_client (uint64_t now_ms)
{
    const int socket_id = accept(g_listener, NULL, NULL);
    size_t    index     = 0U;
    if (0 > socket_id)
    {
        return;
    }
    while ((2U > index) && (C3HTTPS_FREE != g_clients[index].state))
    {
        ++index;
    }
    if ((2U == index) || (0 > fcntl(socket_id, F_SETFL, O_NONBLOCK)))
    {
        /* Full/failed accepted sockets have never transferred ownership. */
        (void)close(socket_id);
        return;
    }
    c3https_client_t * p_client = &g_clients[index];
    p_client->socket            = socket_id;
    p_client->state             = C3HTTPS_HANDSHAKE;
    p_client->since_ms          = now_ms;
    p_client->p_tls             = esp_tls_init();
    if ((NULL == p_client->p_tls) ||
        (ESP_OK != esp_tls_server_session_init(
                       &g_tls_config, socket_id, p_client->p_tls)))
    {
        close_client(index);
    }
}

/** @brief Pinned server delete frees TLS state but does not close its fd. */
static void close_client (size_t index)
{
    c3https_client_t * p_client = &g_clients[index];
    if ((int)index == g_uploader)
    {
        cfgmux_abort(g_config.p_mux);
        g_uploader = -1;
    }
    if (C3HTTPS_FREE != p_client->state)
    {
        if (NULL != p_client->p_tls)
        {
            esp_tls_server_session_delete(p_client->p_tls);
        }
        /* No retry: close may already have invalidated the descriptor. */
        (void)close(p_client->socket);
    }
    memset(p_client, 0, sizeof(*p_client));
    if ((int)index == g_writer)
    {
        g_writer     = -1;
        g_reply_size = 0U;
        g_sent       = 0U;
    }
}

/** @brief One handshake/read/write call per occupied client and iteration. */
static void client_step (size_t index, uint64_t now_ms)
{
    c3https_client_t * p_client = &g_clients[index];
    int                result   = 0;
    if (C3HTTPS_FREE == p_client->state)
    {
        return;
    }
    if (((int)index == g_writer) && (UINT64_MAX == p_client->since_ms))
    {
        /* Activation/save time is not charged to response delivery. */
        p_client->since_ms = now_ms;
    }
    if ((now_ms < p_client->since_ms) ||
        ((now_ms - p_client->since_ms) >= 10000U))
    {
        close_client(index);
        return;
    }
    if ((int)index == g_writer)
    {
        const ssize_t written = esp_tls_conn_write(
            p_client->p_tls, &g_reply[g_sent], g_reply_size - g_sent);
        if ((0 < written) && ((size_t)written <= (g_reply_size - g_sent)))
        {
            g_sent += (size_t)written;
            if (g_sent == g_reply_size)
            {
                close_client(index);
            }
        }
        else if (!retry((int)written))
        {
            close_client(index);
        }
        else
        {
            /* TLS owns a partial record; retry the same remaining bytes. */
        }
    }
    else if (C3HTTPS_HANDSHAKE == p_client->state)
    {
        result = esp_tls_server_session_continue_async(p_client->p_tls);
        if (0 == result)
        {
            result = nethttp_init(
                &p_client->http, g_config.p_token, g_config.token_size, now_ms);
            if (0 == result)
            {
                p_client->state    = C3HTTPS_RECEIVE;
                p_client->since_ms = now_ms;
            }
        }
        if ((0 != result) && (!retry(result)))
        {
            close_client(index);
        }
    }
    else if ((C3HTTPS_RECEIVE == p_client->state) ||
             (C3HTTPS_BODY == p_client->state))
    {
        const ssize_t received =
            esp_tls_conn_read(p_client->p_tls, g_receive, sizeof(g_receive));
        if ((0 < received) && ((size_t)received <= sizeof(g_receive)))
        {
            receive_bytes(index, (size_t)received, now_ms);
        }
        else if (!retry((int)received))
        {
            close_client(index);
        }
        else
        {
            /* Nonblocking receive has no data yet. */
        }
    }
    else
    {
        /* Ready clients wait for the single bounded response buffer. */
    }
}

/** @brief Preserve nonblocking WANT results; EOF and all other errors close. */
static bool retry (int result)
{
    return (ESP_TLS_ERR_SSL_WANT_READ == result) ||
           (ESP_TLS_ERR_SSL_WANT_WRITE == result);
}

/** @brief Serialize observation and response ownership in the controller task.
 */
static int response (size_t                 index,
                     const netread_info_t * p_info,
                     netread_capture_t *    p_capture,
                     void *                 p_context)
{
    const nethttp_route_t route = g_clients[index].http.route;
    if ((NETHTTP_CONFIG_APPLY == route) || (NETHTTP_CONFIG_STATE == route) ||
        (NETHTTP_CONFIG_RESULT == route))
    {
        if (0 != config_reply(index, p_info))
        {
            return -1;
        }
        g_sent                    = 0U;
        g_writer                  = (int)index;
        g_clients[index].since_ms = UINT64_MAX;
        return (NETHTTP_CONFIG_APPLY == route) ? 1 : 0;
    }
    if (0 != netread_reply(&g_clients[index].http,
                           p_info,
                           p_capture,
                           p_context,
                           g_reply,
                           sizeof(g_reply),
                           &g_reply_size))
    {
        return -1;
    }
    g_sent   = 0U;
    g_writer = (int)index;
    return 0;
}

/** @brief Authenticate complete headers before leasing any upload storage. */
static void receive_bytes (size_t index, size_t count, uint64_t now_ms)
{
    c3https_client_t * p_client = &g_clients[index];
    size_t             used     = 0U;
    while ((used < count) && (C3HTTPS_RECEIVE == p_client->state))
    {
        const int status =
            nethttp_feed(&p_client->http, &g_receive[used], 1U, now_ms);
        ++used;
        if (0 != status)
        {
            if ((200 == status) &&
                (NETHTTP_CONFIG_APPLY == p_client->http.route))
            {
                nethttp_change_t change   = {0};
                int              admitted = 400;
                if (0 == nethttp_change(&p_client->http, &change))
                {
                    admitted = cfgmux_begin(g_config.p_mux,
                                            change.boot_id,
                                            change.request_id,
                                            change.revision,
                                            change.expected,
                                            change.p_token,
                                            p_client->http.content_size);
                }
                if (200 == admitted)
                {
                    g_uploader      = (int)index;
                    p_client->state = C3HTTPS_BODY;
                }
                else
                {
                    p_client->http.status = admitted;
                    p_client->state       = C3HTTPS_READY;
                }
            }
            else
            {
                if ((200 == status) && (used != count))
                {
                    p_client->http.status = 400;
                }
                p_client->state    = C3HTTPS_READY;
                p_client->since_ms = now_ms;
            }
        }
    }
    if (C3HTTPS_BODY == p_client->state)
    {
        const int status =
            cfgmux_write(g_config.p_mux, &g_receive[used], count - used);
        if (200 != status)
        {
            cfgmux_abort(g_config.p_mux);
            g_uploader            = -1;
            p_client->http.status = status;
            p_client->state       = C3HTTPS_READY;
        }
        else if (g_config.p_mux->used == g_config.p_mux->upload_size)
        {
            p_client->state = C3HTTPS_READY;
        }
        else
        {
            /* Header and body share the same absolute receive deadline. */
        }
    }
}

/** @brief Configuration responses expose controller facts, never profile bytes.
 */
static int config_reply (size_t index, const netread_info_t * p_info)
{
    nethttp_t * p_http = &g_clients[index].http;
    cfgmux_t *  p_mux  = g_config.p_mux;
    if (200 != p_http->status)
    {
        return error_reply(p_http->status);
    }
    if ((NULL == p_mux) || (!p_mux->b_initialized) ||
        (p_mux->boot_id != p_info->meta.boot_id))
    {
        return error_reply(503);
    }
    if (NETHTTP_CONFIG_STATE == p_http->route)
    {
        return state_reply(p_mux);
    }
    nethttp_change_t change    = {0};
    uint32_t         fields[6] = {UINT32_MAX, 0U, 0U, 0U, 0U, 0U};
    if (0 != nethttp_change(p_http, &change))
    {
        return error_reply(400);
    }
    int status = 503;
    if (NETHTTP_CONFIG_APPLY == p_http->route)
    {
        if ((int)index == g_uploader)
        {
            status     = cfgmux_finish(p_mux, fields);
            g_uploader = -1;
        }
        if (UINT32_MAX == fields[0])
        {
            return error_reply(status);
        }
    }
    else
    {
        status =
            cfgmux_result(p_mux, change.request_id, change.p_token, fields);
        if (200 != status)
        {
            return error_reply(status);
        }
    }
    return result_reply(p_mux, change.request_id, fields, status);
}

/** @brief Read-only global sequence discovery does not evict any retry cache.
 */
static int state_reply (const cfgmux_t * p_mux)
{
    char              body[320] = {0};
    const cfgctrl_t * p_control = p_mux->p_control;
    const int         length    = snprintf(
        body,
        sizeof(body),
        "{\"version\":1,\"boot_id\":\"%016" PRIx64
        "\",\"last_request_id\":%" PRIu32 ",\"active_revision\":%" PRIu32
        ",\"saved_revision\":%" PRIu32 ",\"durable_known\":%s,\"recovery\":%s,"
                   "\"busy\":%s,\"max_body\":8192}",
        p_mux->boot_id,
        p_control->last_id,
        p_control->active_revision,
        p_control->saved_revision,
        p_control->b_saved_known ? "true" : "false",
        p_control->b_recovery ? "true" : "false",
        p_mux->b_lease ? "true" : "false");
    if ((0 > length) || ((size_t)length >= sizeof(body)))
    {
        return -1;
    }
    return nethttp_reply(200,
                         false,
                         (const uint8_t *)body,
                         (size_t)length,
                         g_reply,
                         sizeof(g_reply),
                         &g_reply_size);
}

/** @brief Revisions/flags are the retained result, not a promise of live state.
 */
static int result_reply (const cfgmux_t * p_mux,
                         uint32_t         request_id,
                         const uint32_t * p_fields,
                         int              status)
{
    char      body[320] = {0};
    const int length    = snprintf(
        body,
        sizeof(body),
        "{\"version\":1,\"boot_id\":\"%016" PRIx64 "\",\"request_id\":%" PRIu32
        ",\"controller_status\":%" PRIu32 ",\"active_revision\":%" PRIu32
        ",\"saved_revision\":%" PRIu32 ",\"active_crc\":%" PRIu32
        ",\"saved_crc\":%" PRIu32 ",\"flags\":%" PRIu32 ",\"historical\":%s}",
        p_mux->boot_id,
        request_id,
        p_fields[0],
        p_fields[1],
        p_fields[2],
        p_fields[3],
        p_fields[4],
        p_fields[5],
        (p_mux->p_control->last_id != request_id) ? "true" : "false");
    if ((0 > length) || ((size_t)length >= sizeof(body)))
    {
        return -1;
    }
    return nethttp_reply(status,
                         false,
                         (const uint8_t *)body,
                         (size_t)length,
                         g_reply,
                         sizeof(g_reply),
                         &g_reply_size);
}

/** @brief Authentication/framing/conflict errors contain no submitted bytes. */
static int error_reply (int status)
{
    const uint8_t body[] =
        "{\"version\":1,\"error\":\"configuration_unavailable\"}";
    return nethttp_reply(status,
                         false,
                         body,
                         sizeof(body) - 1U,
                         g_reply,
                         sizeof(g_reply),
                         &g_reply_size);
}
#endif

/* End of file. */

