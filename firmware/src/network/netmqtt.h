/**
 * @file netmqtt.h
 * @brief Bounded MQTT 3.1.1 QoS-zero telemetry packet staging.
 */
#ifndef UTHOME_NETMQTT_H
#define UTHOME_NETMQTT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTHOME_MQTT_TOPIC_BYTES 128U
#ifndef UTHOME_MQTT_VALUE_BYTES
#define UTHOME_MQTT_VALUE_BYTES 1024U
#endif
#ifndef UTHOME_MQTT_PACKET_BYTES
#define UTHOME_MQTT_PACKET_BYTES 1280U
#endif

typedef struct
{
    uint8_t  pending[UTHOME_MQTT_PACKET_BYTES];
    size_t   size;
    uint32_t generation;
    uint32_t replaced;
    bool     b_pending;
} netmqtt_t;

/* Zero-initialize application-lifetime storage. All calls require one owner or
 * external serialization. Input/output storage must not alias this context.
 * Topic is explicit-length printable ASCII, without wildcards. Payload is
 * binary and may contain NUL. No SDK, socket, task or heap operation occurs.
 * Success replaces only pending data; an active socket write owns a separate
 * copied packet. Failed offers leave pending data and counters unchanged. */
int netmqtt_offer (netmqtt_t *     p_mqtt,
                   char const *    p_topic,
                   size_t          topic_size,
                   uint8_t const * p_value,
                   size_t          value_size,
                   uint32_t        generation);
/* Copy one complete packet to caller-owned transmit storage. A stale generation
 * discards pending data and returns 1; empty returns 1. Capacity/argument error
 * returns -1 without consuming. Success returns 0 and consumes pending data.
 * Cancel any active copied packet separately when reconnecting/changing plans.
 */
int netmqtt_take (netmqtt_t * p_mqtt,
                  uint32_t    generation,
                  uint8_t *   p_packet,
                  size_t      capacity,
                  size_t *    p_size);
#endif

/* End of file. */

