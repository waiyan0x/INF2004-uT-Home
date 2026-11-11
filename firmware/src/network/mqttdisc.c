/**
 * @file mqttdisc.c
 * @brief Format bounded discovery and state text without libc formatting.
 */
#include "mqttdisc.h"
#include <math.h>

/* Largest node identifier, excluding its terminator. */
#define MQTTDISC_NODE_MAX 31U

typedef struct
{
    const char *  p_component;
    const char *  p_suffix;
    const char *  p_template;
    const char *  p_class;
    const char *  p_unit;
    msgbus_type_t type;
    bool          b_measurement;
} mqttdisc_entity_t;

typedef struct
{
    char * p_data;
    size_t capacity;
    size_t size;
    bool   b_overflow;
} mqttdisc_text_t;

/* Suffix order is the discovery step order within each slot. Units and
 * classes are those Home Assistant lists for each sensor device class; the
 * degree sign stays an ASCII JSON escape. */
static const mqttdisc_entity_t g_entities[UTHOME_MQTTDISC_SUFFIXES] = {
    {"sensor",
     "temperature",
     "{{ value_json.t_c100 / 100 }}",
     "temperature",
     "\\u00b0C",
     UTHOME_SAMPLE_ENVIRONMENT,
     true},
    {"sensor",
     "pressure",
     "{{ value_json.p_pa }}",
     "pressure",
     "Pa",
     UTHOME_SAMPLE_ENVIRONMENT,
     true},
    {"sensor",
     "distance",
     "{{ value_json.d_mm }}",
     "distance",
     "mm",
     UTHOME_SAMPLE_DISTANCE,
     true},
    {"sensor",
     "illuminance",
     "{{ value_json.lx_m / 1000 }}",
     "illuminance",
     "lx",
     UTHOME_SAMPLE_LIGHT,
     true},
    {"binary_sensor",
     "occupancy",
     "{{ 'ON' if value_json.n > 0 else 'OFF' }}",
     "occupancy",
     NULL,
     UTHOME_SAMPLE_TARGETS,
     false},
    {"sensor",
     "targets",
     "{{ value_json.n }}",
     NULL,
     NULL,
     UTHOME_SAMPLE_TARGETS,
     true},
    {"sensor",
     "raw",
     "{{ value_json.hex }}",
     NULL,
     NULL,
     UTHOME_SAMPLE_RAW,
     false}};

/* Lowercase digits for RAW byte text. */
static const char g_hex[] = "0123456789abcdef";

static bool node_valid (const char * p_node);
static bool name_valid (const char * p_name, size_t capacity);
static void put_char (mqttdisc_text_t * p_text, char value);
static void put_text (mqttdisc_text_t * p_text, const char * p_value);
static void put_unsigned (mqttdisc_text_t * p_text, uint32_t value);
static void put_signed (mqttdisc_text_t * p_text, int32_t value);
static void put_unique (mqttdisc_text_t *         p_text,
                        const char *              p_node,
                        uint16_t                  slot,
                        const mqttdisc_entity_t * p_entity);
static int close_topic (const mqttdisc_text_t * p_text,
                        mqttdisc_message_t *    p_message);
static int config_payload (const char *              p_node,
                           const sampcache_entry_t * p_entry,
                           uint16_t                  slot,
                           const mqttdisc_entity_t * p_entity,
                           mqttdisc_message_t *      p_message);
static int state_values (const sampcache_entry_t * p_entry,
                         mqttdisc_text_t *         p_text);

/** @brief Map a discovery step to its plan position. */
uint16_t mqttdisc_slot (uint32_t step)
{
    /* Steps below the bound give 0..7, which fits uint16_t. */
    return (UTHOME_MQTTDISC_STEPS > step)
               ? (uint16_t)(step / UTHOME_MQTTDISC_SUFFIXES)
               : UTHOME_SAMPLE_NO_SOURCE;
}

/** @brief Format one config or removal message for a slot/suffix pair. */
int mqttdisc_config (uint32_t                  step,
                     const char *              p_node,
                     const sampcache_entry_t * p_entry,
                     mqttdisc_message_t *      p_message)
{
    if ((NULL == p_message) || (UTHOME_MQTTDISC_STEPS <= step) ||
        (!node_valid(p_node)))
    {
        return -1;
    }
    const uint16_t            slot = mqttdisc_slot(step);
    const mqttdisc_entity_t * p_entity =
        &g_entities[step % UTHOME_MQTTDISC_SUFFIXES];
    if ((NULL != p_entry) &&
        ((!p_entry->b_expected) || (slot != p_entry->source.source_index) ||
         (!name_valid(p_entry->source.name, sizeof(p_entry->source.name)))))
    {
        return -1;
    }
    mqttdisc_text_t topic = {.p_data   = p_message->topic,
                             .capacity = sizeof(p_message->topic) - 1U};
    put_text(&topic, "homeassistant/");
    put_text(&topic, p_entity->p_component);
    put_char(&topic, '/');
    put_unique(&topic, p_node, slot, p_entity);
    put_text(&topic, "/config");
    if (0 != close_topic(&topic, p_message))
    {
        return -1;
    }
    p_message->payload_size = 0U;
    if ((NULL == p_entry) || (p_entity->type != p_entry->source.type))
    {
        /* Empty retained payload: Home Assistant deletes any old entity. */
        return 0;
    }
    return config_payload(p_node, p_entry, slot, p_entity, p_message);
}

/** @brief Format integer JSON for one fresh OK source sample. */
int mqttdisc_state (const char *              p_node,
                    const sampcache_entry_t * p_entry,
                    uint64_t                  now_ms,
                    mqttdisc_message_t *      p_message)
{
    if ((NULL == p_entry) || (NULL == p_message) || (!node_valid(p_node)))
    {
        return -1;
    }
    if (1U != sampcache_quality(p_entry, now_ms))
    {
        return 1;
    }
    if (!name_valid(p_entry->source.name, sizeof(p_entry->source.name)))
    {
        return -1;
    }
    mqttdisc_text_t topic = {.p_data   = p_message->topic,
                             .capacity = sizeof(p_message->topic) - 1U};
    put_text(&topic, "uthome/v1/");
    put_text(&topic, p_node);
    put_text(&topic, "/state/");
    put_text(&topic, p_entry->source.name);
    if (0 != close_topic(&topic, p_message))
    {
        return -1;
    }
    mqttdisc_text_t payload = {.p_data   = p_message->payload,
                               .capacity = sizeof(p_message->payload)};
    const int       result  = state_values(p_entry, &payload);
    if (0 != result)
    {
        return result;
    }
    if (payload.b_overflow)
    {
        return -1;
    }
    p_message->payload_size = payload.size;
    return 0;
}

/** @brief Discovery IDs allow only Home Assistant's [A-Za-z0-9_-] class. */
static bool node_valid (const char * p_node)
{
    if (NULL == p_node)
    {
        return false;
    }
    for (size_t index = 0U; index <= MQTTDISC_NODE_MAX; ++index)
    {
        const char value = p_node[index];
        if ('\0' == value)
        {
            return 0U != index;
        }
        if (!((('A' <= value) && ('Z' >= value)) ||
              (('a' <= value) && ('z' >= value)) ||
              (('0' <= value) && ('9' >= value)) || ('_' == value) ||
              ('-' == value)))
        {
            return false;
        }
    }
    return false;
}

/** @brief Match the configuration identifier rule, so no JSON escaping. */
static bool name_valid (const char * p_name, size_t capacity)
{
    for (size_t index = 0U; index < capacity; ++index)
    {
        const char value    = p_name[index];
        const bool b_letter = (('A' <= value) && ('Z' >= value)) ||
                              (('a' <= value) && ('z' >= value));
        const bool b_extra = (('0' <= value) && ('9' >= value)) ||
                             ('_' == value) || ('.' == value) || ('-' == value);
        if ('\0' == value)
        {
            return 0U != index;
        }
        if ((!b_letter) && ((0U == index) || (!b_extra)))
        {
            return false;
        }
    }
    return false;
}

/** @brief Append within capacity, recording overflow instead of truncating. */
static void put_char (mqttdisc_text_t * p_text, char value)
{
    if (p_text->size < p_text->capacity)
    {
        p_text->p_data[p_text->size] = value;
        ++p_text->size;
    }
    else
    {
        p_text->b_overflow = true;
    }
}

/** @brief Append a terminated literal or previously validated identifier. */
static void put_text (mqttdisc_text_t * p_text, const char * p_value)
{
    for (size_t index = 0U; '\0' != p_value[index]; ++index)
    {
        put_char(p_text, p_value[index]);
    }
}

/** @brief Append an unsigned decimal without leading zeros. */
static void put_unsigned (mqttdisc_text_t * p_text, uint32_t value)
{
    char     digits[10] = {0};
    size_t   count      = 0U;
    uint32_t remaining  = value;
    do
    {
        /* Each remainder is 0..9, so the character stays a digit. */
        digits[count] = (char)('0' + (char)(remaining % 10U));
        ++count;
        remaining /= 10U;
    } while (0U != remaining);
    while (0U != count)
    {
        --count;
        put_char(p_text, digits[count]);
    }
}

/** @brief Append a signed decimal, including INT32_MIN without overflow. */
static void put_signed (mqttdisc_text_t * p_text, int32_t value)
{
    if (0 > value)
    {
        put_char(p_text, '-');
        /* -(value + 1) is nonnegative for every int32_t value. */
        put_unsigned(p_text, (uint32_t)(-(value + 1)) + 1U);
    }
    else
    {
        put_unsigned(p_text, (uint32_t)value);
    }
}

/** @brief Stable ID from the plan position, never from the display name. */
static void put_unique (mqttdisc_text_t *         p_text,
                        const char *              p_node,
                        uint16_t                  slot,
                        const mqttdisc_entity_t * p_entity)
{
    put_text(p_text, p_node);
    put_text(p_text, "_s");
    put_unsigned(p_text, slot);
    put_char(p_text, '_');
    put_text(p_text, p_entity->p_suffix);
}

/** @brief Terminate topic text; capacity already excludes the terminator. */
static int close_topic (const mqttdisc_text_t * p_text,
                        mqttdisc_message_t *    p_message)
{
    if (p_text->b_overflow)
    {
        return -1;
    }
    p_message->topic[p_text->size] = '\0';
    p_message->topic_size          = p_text->size;
    return 0;
}

/** @brief Entity config grouped under one device keyed by the node ID.
 *
 * Uses Home Assistant's documented discovery abbreviations: uniq_id unique_id,
 * stat_t state_topic, val_tpl value_template, dev_cla device_class,
 * unit_of_meas unit_of_measurement, stat_cla state_class, exp_aft
 * expire_after, avty_t availability_topic, dev device; in dev, ids
 * identifiers, mf manufacturer, mdl model. "~" is the base topic
 * uthome/v1/<node>; Home Assistant expands a leading "~" in *_topic values.
 */
static int config_payload (const char *              p_node,
                           const sampcache_entry_t * p_entry,
                           uint16_t                  slot,
                           const mqttdisc_entity_t * p_entity,
                           mqttdisc_message_t *      p_message)
{
    mqttdisc_text_t text = {.p_data   = p_message->payload,
                            .capacity = sizeof(p_message->payload)};
    put_text(&text, "{\"~\":\"uthome/v1/");
    put_text(&text, p_node);
    put_text(&text, "\",\"name\":\"");
    put_text(&text, p_entry->source.name);
    put_char(&text, ' ');
    put_text(&text, p_entity->p_suffix);
    put_text(&text, "\",\"uniq_id\":\"");
    put_unique(&text, p_node, slot, p_entity);
    put_text(&text, "\",\"stat_t\":\"~/state/");
    put_text(&text, p_entry->source.name);
    put_text(&text, "\",\"val_tpl\":\"");
    put_text(&text, p_entity->p_template);
    put_char(&text, '"');
    if (NULL != p_entity->p_class)
    {
        put_text(&text, ",\"dev_cla\":\"");
        put_text(&text, p_entity->p_class);
        put_char(&text, '"');
    }
    if (NULL != p_entity->p_unit)
    {
        put_text(&text, ",\"unit_of_meas\":\"");
        put_text(&text, p_entity->p_unit);
        put_char(&text, '"');
    }
    if (p_entity->b_measurement)
    {
        put_text(&text, ",\"stat_cla\":\"measurement\"");
    }
    put_text(&text, ",\"exp_aft\":");
    put_unsigned(&text, UTHOME_MQTTDISC_EXPIRE_S);
    put_text(&text, ",\"avty_t\":\"~/availability\",\"dev\":{\"ids\":[\"");
    put_text(&text, p_node);
    put_text(&text, "\"],\"name\":\"");
    put_text(&text, p_node);
    put_text(&text, "\",\"mf\":\"uT-Home\",\"mdl\":\"ESP32-C3\"}}");
    if (text.b_overflow)
    {
        return -1;
    }
    p_message->payload_size = text.size;
    return 0;
}

/** @brief Scaled integers only; out-of-range values publish nothing. */
static int state_values (const sampcache_entry_t * p_entry,
                         mqttdisc_text_t *         p_text)
{
    const sampcache_value_t * p_value = &p_entry->value;
    if (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type)
    {
        const float centi    = p_value->environment.temperature_c * 100.0f;
        const float pressure = p_value->environment.pressure_pa;
        if ((!isfinite(centi)) || (!isfinite(pressure)) ||
            (-100000.0f > centi) || (100000.0f < centi) || (0.0f > pressure) ||
            (2000000.0f < pressure))
        {
            return 1;
        }
        /* Checked ranges keep both rounded values inside their integer types.
         */
        const int32_t temperature =
            (int32_t)((0.0f > centi) ? (centi - 0.5f) : (centi + 0.5f));
        put_text(p_text, "{\"t_c100\":");
        put_signed(p_text, temperature);
        put_text(p_text, ",\"p_pa\":");
        put_unsigned(p_text, (uint32_t)(pressure + 0.5f));
    }
    else if (UTHOME_SAMPLE_DISTANCE == p_entry->source.type)
    {
        put_text(p_text, "{\"d_mm\":");
        put_unsigned(p_text, p_value->distance.distance_mm);
    }
    else if (UTHOME_SAMPLE_LIGHT == p_entry->source.type)
    {
        if (UINT32_C(200000000) < p_value->light.millilux)
        {
            return 1;
        }
        put_text(p_text, "{\"lx_m\":");
        put_unsigned(p_text, p_value->light.millilux);
    }
    else if (UTHOME_SAMPLE_TARGETS == p_entry->source.type)
    {
        uint32_t present = 0U;
        for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
        {
            if (p_value->targets.targets[index].b_present)
            {
                ++present;
            }
        }
        put_text(p_text, "{\"n\":");
        put_unsigned(p_text, present);
    }
    else if (UTHOME_SAMPLE_RAW == p_entry->source.type)
    {
        if ((1U > p_value->raw.length) ||
            (UTHOME_SAMPLE_RAW_MAX < p_value->raw.length))
        {
            return 1;
        }
        put_text(p_text, "{\"hex\":\"");
        for (size_t index = 0U; index < p_value->raw.length; ++index)
        {
            const uint8_t value = p_value->raw.bytes[index];
            put_char(p_text, g_hex[value >> 4U]);
            put_char(p_text, g_hex[value & 0x0fU]);
        }
        put_char(p_text, '"');
    }
    else
    {
        /* HEALTH and unknown types are not exposed as entities. */
        return 1;
    }
    put_char(p_text, '}');
    return 0;
}

/* End of file. */

