/**
 * @file nethttp.h
 * @brief Bounded authenticated HTTP read and configuration-header admission.
 */
#ifndef UTHOME_NETHTTP_H
#define UTHOME_NETHTTP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTHOME_HTTP_HEADERS 1024U
#define UTHOME_HTTP_SECRET  64U

typedef enum
{
    NETHTTP_NONE = 0,
    NETHTTP_IDENTITY,
    NETHTTP_STATUS,
    NETHTTP_TELEMETRY,
    NETHTTP_CONFIG_STATE,
    NETHTTP_CONFIG_APPLY,
    NETHTTP_CONFIG_RESULT
} nethttp_route_t;

typedef struct
{
    char            headers[UTHOME_HTTP_HEADERS + 1U];
    uint8_t         secret[UTHOME_HTTP_SECRET];
    size_t          used;
    size_t          secret_size;
    uint64_t        started_ms;
    uint64_t        last_ms;
    int             status;
    nethttp_route_t route;
    uint16_t        source;
    uint32_t        content_size;
} nethttp_t;

typedef struct
{
    uint64_t     boot_id;
    uint32_t     request_id;
    uint32_t     revision;
    uint32_t     expected;
    const char * p_token;
} nethttp_change_t;

/* Read validated transaction headers after authenticated admission only.
 * Token pointer borrows the connection's immutable parsed header storage.
 */
int nethttp_change (const nethttp_t * p_http, nethttp_change_t * p_change);

/* One caller-owned context per connection, one owner, no aliasing of buffers.
 * Initialize at accept with 16..64 visible ASCII secret bytes. The socket/TLS
 * owner must erase the context on release and enforce the connection limit.
 * Neither function allocates, creates objects, calls I/O nor applies config.
 */
int nethttp_init (nethttp_t *     p_http,
                  uint8_t const * p_secret,
                  size_t          secret_size,
                  uint64_t        now_ms);
/* 0 needs more headers; 200 authenticated read admitted; otherwise HTTP error.
 * Absolute 10-second deadline, <=1024 header bytes, <=16 fields, <=256-byte
 * lines, HTTP/1.1 only, Host required, one request then close. No bodies,
 * pipelining or transfer encodings. Configuration POST admits headers only;
 * the transport must authenticate before leasing a bounded body and dispatch
 * through cfgmux. Nonzero bodies remain forbidden on reads.
 * Poll with size zero to enforce the receive deadline even during silence.
 * A terminal context cannot be reused: later feed returns -1.
 */
int nethttp_feed (nethttp_t *     p_http,
                  uint8_t const * p_data,
                  size_t          size,
                  uint64_t        now_ms);
/* Construct one complete HTTP/1.1 reply with Connection: close. Content types
 * are selected by a Boolean, never interpolated untrusted headers. Binary body
 * is copied explicitly; failed capacity checks do not write output. Caller
 * retains output unchanged until all partial socket writes complete.
 */
int nethttp_reply (int             status,
                   bool            b_binary,
                   uint8_t const * p_body,
                   size_t          body_size,
                   uint8_t *       p_output,
                   size_t          capacity,
                   size_t *        p_size);
#endif

/* End of file. */

