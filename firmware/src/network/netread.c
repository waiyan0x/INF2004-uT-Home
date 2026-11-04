/**
 * @file netread.c
 * @brief Serve copied sensor information while preserving controller ownership.
 */
#include "netread.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int sensor (nethttp_t const *      p_http,
                   netread_info_t const * p_info,
                   netread_capture_t *    p_capture,
                   void *                 p_context,
                   uint8_t *              p_output,
                   size_t                 capacity,
                   size_t *               p_size);
static bool identity (char const * p_name);
static int
error_reply (int status, uint8_t * p_output, size_t capacity, size_t * p_size);

/** @brief Dispatch supported authenticated reads without invoking mutation. */
int netread_reply (nethttp_t const *      p_http,
                   netread_info_t const * p_info,
                   netread_capture_t *    p_capture,
                   void *                 p_context,
                   uint8_t *              p_output,
                   size_t                 capacity,
                   size_t *               p_size)
{
    char body[384] = {0};
    int  length    = 0;
    if ((NULL == p_http) || (NULL == p_output) || (NULL == p_size) ||
        (100 > p_http->status) || (599 < p_http->status))
    {
        return -1;
    }
    if (200 != p_http->status)
    {
        return error_reply(p_http->status, p_output, capacity, p_size);
    }
    if ((NULL == p_info) || (!identity(p_info->node_id)))
    {
        return -1;
    }
    if (NETHTTP_TELEMETRY == p_http->route)
    {
        return sensor(
            p_http, p_info, p_capture, p_context, p_output, capacity, p_size);
    }
    if (NETHTTP_IDENTITY == p_http->route)
    {
        length = snprintf(
            body,
            sizeof(body),
            "{\"version\":1,\"node_id\":\"%s\",\"boot_id\":\"%016" PRIx64
            "\",\"configuration_supported\":%s}",
            p_info->node_id,
            p_info->meta.boot_id,
            p_info->b_configuration_supported ? "true" : "false");
    }
    else if (NETHTTP_STATUS == p_http->route)
    {
        length = snprintf(
            body,
            sizeof(body),
            "{\"version\":1,\"boot_id\":\"%016" PRIx64
            "\",\"active_revision\":%" PRIu32 ",\"saved_revision\":%" PRIu32
            ",\"runtime_generation\":%" PRIu32 ",\"runtime_state\":%" PRIu32
            ",\"durable_known\":%s,\"wifi_online\":%s,"
            "\"mqtt\":{\"enabled\":%s,\"state\":%" PRIu32
            ",\"failures\":%" PRIu32 ",\"replaced\":%" PRIu32
            ",\"discarded\":%" PRIu32 "}}",
            p_info->meta.boot_id,
            p_info->meta.active_revision,
            p_info->saved_revision,
            p_info->meta.generation,
            p_info->runtime_state,
            p_info->b_durable_known ? "true" : "false",
            p_info->b_wifi_online ? "true" : "false",
            p_info->b_mqtt_enabled ? "true" : "false",
            p_info->mqtt.state,
            p_info->mqtt.failures,
            p_info->mqtt.replaced,
            p_info->mqtt.discarded);
    }
    else
    {
        return error_reply(404, p_output, capacity, p_size);
    }
    if ((0 > length) || ((size_t)length >= sizeof(body)))
    {
        return -1;
    }
    return nethttp_reply(200,
                         false,
                         (uint8_t const *)body,
                         (size_t)length,
                         p_output,
                         capacity,
                         p_size);
}

/** @brief Capture only one copied source with explicit busy/absent results. */
static int sensor (nethttp_t const *      p_http,
                   netread_info_t const * p_info,
                   netread_capture_t *    p_capture,
                   void *                 p_context,
                   uint8_t *              p_output,
                   size_t                 capacity,
                   size_t *               p_size)
{
    sampcache_entry_t entry                   = {0};
    uint64_t          now_ms                  = 0U;
    uint8_t           frame[UTHOME_BLE_FRAME] = {0};
    int               result                  = -1;
    if (NULL != p_capture)
    {
        result = p_capture(p_context, p_http->source, &entry, &now_ms);
    }
    if (0 != result)
    {
        return error_reply(
            (1 == result) ? 404 : 503, p_output, capacity, p_size);
    }
    if ((entry.source.source_index != p_http->source) ||
        (0 !=
         netble_encode(&entry, &p_info->meta, now_ms, frame, sizeof(frame))))
    {
        return error_reply(503, p_output, capacity, p_size);
    }
    return nethttp_reply(
        200, true, frame, sizeof(frame), p_output, capacity, p_size);
}

/** @brief Restrict node identity so JSON cannot contain injected delimiters. */
static bool identity (char const * p_name)
{
    if ((NULL == memchr(p_name, '\0', 32U)) || ('\0' == p_name[0]))
    {
        return false;
    }
    for (size_t index = 0U; '\0' != p_name[index]; ++index)
    {
        const unsigned char character = (unsigned char)p_name[index];
        if ((!((('a' <= character) && ('z' >= character)) ||
               (('A' <= character) && ('Z' >= character)) ||
               (('0' <= character) && ('9' >= character)))) &&
            ('_' != character) && ('-' != character))
        {
            return false;
        }
    }
    return true;
}

/** @brief Preserve error status without leaking submitted credentials. */
static int
error_reply (int status, uint8_t * p_output, size_t capacity, size_t * p_size)
{
    const uint8_t body[] = "{\"version\":1,\"error\":\"request_unavailable\"}";
    return nethttp_reply(
        status, false, body, sizeof(body) - 1U, p_output, capacity, p_size);
}

/* End of file. */

