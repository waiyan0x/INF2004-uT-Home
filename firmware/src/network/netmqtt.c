/**
 * @file netmqtt.c
 * @brief Encode complete bounded QoS-zero publish packets without allocation.
 */
#include "netmqtt.h"
#include <string.h>

/** @brief Validate all lengths before replacing pending telemetry. */
int netmqtt_offer (netmqtt_t *     p_mqtt,
                   char const *    p_topic,
                   size_t          topic_size,
                   uint8_t const * p_value,
                   size_t          value_size,
                   uint32_t        generation,
                   bool            b_retain)
{
    size_t remaining = 0U;
    size_t offset    = 1U;
    size_t encoded   = 0U;
    if ((NULL == p_mqtt) || (NULL == p_topic) ||
        ((NULL == p_value) && (0U != value_size)) || (0U == topic_size) ||
        (UTHOME_MQTT_TOPIC_BYTES < topic_size) ||
        (UTHOME_MQTT_VALUE_BYTES < value_size))
    {
        return -1;
    }
    for (size_t index = 0U; index < topic_size; ++index)
    {
        const unsigned char character = (unsigned char)p_topic[index];
        if ((0x21U > character) || (0x7eU < character) || ('+' == character) ||
            ('#' == character))
        {
            return -1;
        }
    }
    remaining          = 2U + topic_size + value_size;
    encoded            = remaining;
    p_mqtt->pending[0] = b_retain ? (UTHOME_MQTT_PUBLISH | UTHOME_MQTT_RETAIN)
                                  : UTHOME_MQTT_PUBLISH;
    do
    {
        uint8_t digit = (uint8_t)(encoded % 128U);
        encoded /= 128U;
        if (0U != encoded)
        {
            digit |= 0x80U;
        }
        p_mqtt->pending[offset] = digit;
        ++offset;
    } while (0U != encoded);
    /* Bounds above limit remaining to 1154 and complete packets to 1157. */
    p_mqtt->pending[offset] = (uint8_t)(topic_size >> 8U);
    ++offset;
    p_mqtt->pending[offset] = (uint8_t)(topic_size & 0xffU);
    ++offset;
    memcpy(&p_mqtt->pending[offset], p_topic, topic_size);
    offset += topic_size;
    if (0U != value_size)
    {
        memcpy(&p_mqtt->pending[offset], p_value, value_size);
    }
    if ((p_mqtt->b_pending) && (UINT32_MAX > p_mqtt->replaced))
    {
        ++p_mqtt->replaced;
    }
    p_mqtt->size       = offset + value_size;
    p_mqtt->generation = generation;
    p_mqtt->b_pending  = true;
    return 0;
}

/** @brief Preserve packet ownership across bounded partial socket writes. */
int netmqtt_take (netmqtt_t * p_mqtt,
                  uint32_t    generation,
                  uint8_t *   p_packet,
                  size_t      capacity,
                  size_t *    p_size)
{
    if ((NULL == p_mqtt) || (NULL == p_packet) || (NULL == p_size))
    {
        return -1;
    }
    *p_size = 0U;
    if ((!p_mqtt->b_pending) || (generation != p_mqtt->generation))
    {
        p_mqtt->b_pending = false;
        p_mqtt->size      = 0U;
        return 1;
    }
    if ((UTHOME_MQTT_PACKET_BYTES < p_mqtt->size) || (capacity < p_mqtt->size))
    {
        return -1;
    }
    memcpy(p_packet, p_mqtt->pending, p_mqtt->size);
    *p_size           = p_mqtt->size;
    p_mqtt->b_pending = false;
    p_mqtt->size      = 0U;
    return 0;
}

/* End of file. */

