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
int c3mqtt_step (uint64_t               now_ms,
                 bool                   b_ip,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context);
/* Single owner, copied diagnostics only; safe before startup. */
int c3mqtt_status (netbroker_view_t * p_view);
#endif

/* End of file. */

