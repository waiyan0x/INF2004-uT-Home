/**
 * @file netbroker.h
 * @brief One bounded MQTT 3.1.1 publisher session over caller-owned TLS I/O.
 */
#ifndef UTHOME_NETBROKER_H
#define UTHOME_NETBROKER_H
#include "netmqtt.h"
#include "netretry.h"

typedef int netbroker_connect_t (void * p_context);
typedef int netbroker_read_t (void * p_context, uint8_t * p_data, size_t size);
typedef int
netbroker_write_t (void * p_context, const uint8_t * p_data, size_t size);
typedef void netbroker_close_t (void * p_context);

typedef struct
{
    netbroker_connect_t * p_connect;
    netbroker_read_t *    p_read;
    netbroker_write_t *   p_write;
    netbroker_close_t *   p_close;
    void *                p_context;
    const char *          p_client;
    const char *          p_user;
    const char *          p_password;
    const char *          p_availability;
} netbroker_config_t;

typedef enum
{
    NETBROKER_OFF = 0,
    NETBROKER_TLS,
    NETBROKER_CONNECT,
    NETBROKER_ACK,
    NETBROKER_AVAILABLE,
    NETBROKER_READY,
    NETBROKER_UNAVAILABLE
} netbroker_state_t;

typedef struct
{
    uint32_t state;
    uint32_t failures;
    uint32_t replaced;
    uint32_t discarded;
} netbroker_view_t;

typedef struct
{
    netbroker_config_t config;
    netretry_t         retry;
    netmqtt_t          mailbox;
    uint8_t            transmit[UTHOME_MQTT_PACKET_BYTES];
    uint8_t            receive[4];
    size_t             size;
    size_t             offset;
    size_t             received;
    uint64_t           last_ms;
    uint64_t           deadline_ms;
    uint64_t           sent_ms;
    uint64_t           ping_ms;
    uint32_t           generation;
    uint32_t           failures;
    uint32_t           discarded;
    netbroker_state_t  state;
    bool               b_initialized;
    bool               b_ping;
    bool               b_telemetry;
} netbroker_t;

/* Single owner, zero-initialized lifetime storage, immutable config strings.
 * client/user/password: 1..32/64/64 printable ASCII; availability <=128 ASCII
 * without wildcards. Clean session, 30s keepalive, retained QoS0 availability
 * and Last Will ("online"/"offline"), QoS0 binary telemetry, no subscriptions.
 * connect: 1 verified TLS ready, 0 pending, -1 failure. read/write: positive
 * bytes <= requested, 0 would-block, -1 EOF/error. close releases partial TLS.
 * Callbacks must do one nonblocking unit, never wait/DNS/log secrets.
 */
int netbroker_init (netbroker_t * p_state, const netbroker_config_t * p_config);
int netbroker_step (netbroker_t * p_state,
                    uint64_t      now_ms,
                    bool          b_upstream,
                    uint32_t      generation,
                    uint32_t      jitter_ms);
/* 0 staged, 1 offline/dropped, -1 invalid. One pending plus one active packet;
 * newer offers replace pending only. Disconnect/plan change discards both.
 */
int netbroker_offer (netbroker_t *   p_state,
                     const char *    p_topic,
                     const uint8_t * p_value,
                     size_t          size);
/* Same owner as step/offer, copied only: no I/O, counter reset or mutation.
 * Uninitialized state reports UNAVAILABLE. Counters saturate per boot.
 * failures excludes intentional plan changes; replaced counts superseded
 * pending frames; discarded counts unsent active/pending frames at reset and
 * valid offers while offline. These are not broker delivery acknowledgments.
 */
int netbroker_snapshot (const netbroker_t * p_state, netbroker_view_t * p_view);
#endif

/* End of file. */

