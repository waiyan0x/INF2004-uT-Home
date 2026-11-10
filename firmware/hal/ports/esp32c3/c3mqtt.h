/**
 * @file c3mqtt.h
 * @brief Certificate-verified MQTT TLS publisher owned by the application.
 */
#ifndef UTHOME_C3MQTT_H
#define UTHOME_C3MQTT_H
#include "netread.h"

typedef struct
{
    const char *    p_address;
    const char *    p_server_name;
    const char *    p_user;
    const char *    p_password;
    const uint8_t * p_ca;
    size_t          ca_size;
    uint16_t        port;
} c3mqtt_config_t;

/* Immutable boot-lifetime inputs. IPv4 literal avoids synchronous SDK DNS;
 * server_name remains mandatory for certificate verification/SNI. No NVS,
 * task or controller mutation. SDK TLS allocation remains unqualified.
 */
int c3mqtt_start (const c3mqtt_config_t * p_config, const char * p_node);
/* Owner loop: one broker I/O unit, then at most one offer. After every
 * connect, 56 retained Home Assistant discovery configs/removals are offered
 * one at a time when the broker is idle; then one plan position per second is
 * copied for unchanged binary telemetry and, if fresh OK, JSON state.
 */
int c3mqtt_step (uint64_t               now_ms,
                 bool                   b_ip,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context);
/* Single owner, copied diagnostics only; safe before startup. */
int c3mqtt_status (netbroker_view_t * p_view);
/* Enable the node-specific transaction subscription before connecting. */
int c3mqtt_transactions (void);
bool c3mqtt_ready (void);
int c3mqtt_request (uint8_t * p_data, size_t capacity, size_t * p_size);
/* Priority result mailbox: 1 means backpressure/offline, retain caller copy. */
int c3mqtt_result (const uint8_t * p_data, size_t size);
#endif

/* End of file. */

