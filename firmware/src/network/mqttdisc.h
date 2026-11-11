/**
 * @file mqttdisc.h
 * @brief Home Assistant MQTT discovery and JSON state text for cached sources.
 */
#ifndef UTHOME_MQTTDISC_H
#define UTHOME_MQTTDISC_H
#include "sampcache.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Entity suffixes per source slot, and one discovery step per slot/suffix. */
#define UTHOME_MQTTDISC_SUFFIXES 7U
#define UTHOME_MQTTDISC_STEPS                                                  \
    (UTHOME_SAMPCACHE_SOURCES * UTHOME_MQTTDISC_SUFFIXES)
/* Topic storage includes the terminating NUL required by netbroker_offer. */
#define UTHOME_MQTTDISC_TOPIC_BYTES 128U
/* Largest config payload (31-byte node and name, temperature) is 490 bytes;
 * 10 bytes of margin. Overflow is still rejected, never truncated. */
#define UTHOME_MQTTDISC_PAYLOAD_BYTES 500U
/* Seconds without a state message before Home Assistant marks it unavailable.
 */
#define UTHOME_MQTTDISC_EXPIRE_S 30U

/* Caller-owned output. topic is NUL-terminated printable ASCII. payload is
 * ASCII JSON without a terminator; payload_size 0 means a removal message.
 */
typedef struct
{
    char   topic[UTHOME_MQTTDISC_TOPIC_BYTES];
    char   payload[UTHOME_MQTTDISC_PAYLOAD_BYTES];
    size_t topic_size;
    size_t payload_size;
} mqttdisc_message_t;

/* Return the source slot (0..7) addressed by a discovery step (0..55). */
uint16_t mqttdisc_slot (uint32_t step);

/* Format discovery step = slot * 7 + suffix for the topic
 * homeassistant/<component>/<node>_s<slot>_<suffix>/config. p_entry is the
 * copied source for that slot, or NULL when the slot is absent. The payload is
 * the entity config when the source type owns the suffix, otherwise empty so a
 * retained publish removes any old entity. Node is 1..31 bytes of
 * [A-Za-z0-9_-]; source names follow the configuration identifier rule.
 * Returns 0 formatted, -1 invalid step/node/entry (message contents undefined).
 * Pure formatting: no I/O, SDK, allocation, lock or static state.
 */
int mqttdisc_config (uint32_t                  step,
                     const char *              p_node,
                     const sampcache_entry_t * p_entry,
                     mqttdisc_message_t *      p_message);

/* Format uthome/v1/<node>/state/<name> with integer JSON values for a fresh
 * OK sample (sampcache_quality 1 at now_ms, normally the capture time).
 * Returns 0 formatted; 1 nothing to publish (missing, stale, fault, HEALTH or
 * an out-of-range value); -1 invalid arguments, node or name.
 */
int mqttdisc_state (const char *              p_node,
                    const sampcache_entry_t * p_entry,
                    uint64_t                  now_ms,
                    mqttdisc_message_t *      p_message);
#endif

/* End of file. */

