/**
 * @file nethttp.c
 * @brief Reject unsafe framing before authenticated request admission.
 */
#include "nethttp.h"
#include <stdio.h>
#include <string.h>

static int parse (nethttp_t * p_http);
static bool same (char const * p_left, char const * p_right);
static bool authorized (nethttp_t const * p_http, char const * p_value);
static int route (nethttp_t * p_http, char const * p_path);
static bool field_name (char const * p_name);
static const char * header_value (const nethttp_t * p_http,
                                  const char *      p_name);
static bool decimal (const char * p_text, uint32_t minimum, uint32_t * p_value);
static int transaction_field (const char * p_name);

/** @brief Retain a bounded credential only after validating all inputs. */
int nethttp_init (nethttp_t *     p_http,
                  uint8_t const * p_secret,
                  size_t          secret_size,
                  uint64_t        now_ms)
{
    if ((NULL == p_http) || (NULL == p_secret) || (16U > secret_size) ||
        (UTHOME_HTTP_SECRET < secret_size))
    {
        return -1;
    }
    for (size_t index = 0U; index < secret_size; ++index)
    {
        if ((0x21U > p_secret[index]) || (0x7eU < p_secret[index]))
        {
            return -1;
        }
    }
    memset(p_http, 0, sizeof(*p_http));
    memcpy(p_http->secret, p_secret, secret_size);
    p_http->secret_size = secret_size;
    p_http->started_ms  = now_ms;
    p_http->last_ms     = now_ms;
    return 0;
}

/** @brief Accumulate headers without restarting the receive deadline. */
int nethttp_feed (nethttp_t *     p_http,
                  uint8_t const * p_data,
                  size_t          size,
                  uint64_t        now_ms)
{
    if ((NULL == p_http) || ((NULL == p_data) && (0U != size)) ||
        (0 != p_http->status) || (16U > p_http->secret_size) ||
        (UTHOME_HTTP_SECRET < p_http->secret_size))
    {
        return -1;
    }
    if ((now_ms < p_http->last_ms) || ((now_ms - p_http->started_ms) >= 10000U))
    {
        p_http->status = 408;
        return p_http->status;
    }
    p_http->last_ms = now_ms;
    for (size_t index = 0U; index < size; ++index)
    {
        const uint8_t character = p_data[index];
        if (UTHOME_HTTP_HEADERS <= p_http->used)
        {
            p_http->status = 431;
            return p_http->status;
        }
        if ((('\n' == character) &&
             ((0U == p_http->used) ||
              ('\r' != p_http->headers[p_http->used - 1U]))) ||
            ((0U != p_http->used) &&
             ('\r' == p_http->headers[p_http->used - 1U]) &&
             ('\n' != character)))
        {
            p_http->status = 400;
            return p_http->status;
        }
        if (((0x20U > character) && ('\r' != character) &&
             ('\n' != character)) ||
            (0x7eU < character))
        {
            p_http->status = 400;
            return p_http->status;
        }
        p_http->headers[p_http->used++] = (char)character;
        if ((4U <= p_http->used) &&
            (0 == memcmp(&p_http->headers[p_http->used - 4U], "\r\n\r\n", 4U)))
        {
            p_http->headers[p_http->used] = '\0';
            p_http->status = (index + 1U == size) ? parse(p_http) : 400;
            return p_http->status;
        }
    }
    return 0;
}

/** @brief Generate fixed safe response headers and a bounded binary body. */
int nethttp_reply (int             status,
                   bool            b_binary,
                   uint8_t const * p_body,
                   size_t          body_size,
                   uint8_t *       p_output,
                   size_t          capacity,
                   size_t *        p_size)
{
    char header[256] = {0};
    int  length      = 0;
    if ((NULL == p_output) || (NULL == p_size) ||
        ((NULL == p_body) && (0U != body_size)) || (100 > status) ||
        (599 < status) || (2048U < body_size))
    {
        return -1;
    }
    length = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d Response\r\nContent-Type: %s\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%s\r\n",
        status,
        b_binary ? "application/octet-stream" : "application/json",
        body_size,
        (401 == status) ? "WWW-Authenticate: Bearer\r\n" : "");
    if ((0 > length) || ((size_t)length >= sizeof(header)) ||
        (capacity < (size_t)length) ||
        ((capacity - (size_t)length) < body_size))
    {
        return -1;
    }
    memcpy(p_output, header, (size_t)length);
    if (0U != body_size)
    {
        memcpy(&p_output[(size_t)length], p_body, body_size);
    }
    *p_size = (size_t)length + body_size;
    return 0;
}

/** @brief Decode canonical transaction metadata without body allocation. */
int nethttp_change (const nethttp_t * p_http, nethttp_change_t * p_change)
{
    if ((NULL == p_http) || (NULL == p_change) ||
        ((NETHTTP_CONFIG_APPLY != p_http->route) &&
         (NETHTTP_CONFIG_RESULT != p_http->route)))
    {
        return -1;
    }
    nethttp_change_t change = {0};
    change.p_token          = header_value(p_http, "x-request-token");
    if ((!decimal(
            header_value(p_http, "x-request-id"), 1U, &change.request_id)) ||
        (NULL == change.p_token))
    {
        return -1;
    }
    const size_t length = strlen(change.p_token);
    if ((0U == length) || (64U < length))
    {
        return -1;
    }
    for (size_t index = 0U; index < length; ++index)
    {
        const unsigned char value = (unsigned char)change.p_token[index];
        if (!((('a' <= value) && ('z' >= value)) ||
              (('A' <= value) && ('Z' >= value)) ||
              (('0' <= value) && ('9' >= value)) || ('-' == value) ||
              ('_' == value) || ('.' == value)))
        {
            return -1;
        }
    }
    if (NETHTTP_CONFIG_APPLY == p_http->route)
    {
        const char * p_boot = header_value(p_http, "x-expected-boot-id");
        if ((!decimal(
                header_value(p_http, "x-revision"), 1U, &change.revision)) ||
            (!decimal(header_value(p_http, "x-expected-revision"),
                      0U,
                      &change.expected)) ||
            (NULL == p_boot) || (16U != strlen(p_boot)))
        {
            return -1;
        }
        for (size_t index = 0U; index < 16U; ++index)
        {
            const char value = p_boot[index];
            if (!((('0' <= value) && ('9' >= value)) ||
                  (('a' <= value) && ('f' >= value))))
            {
                return -1;
            }
            const unsigned int digit = ('9' >= value)
                                           ? (unsigned int)(value - '0')
                                           : (unsigned int)(value - 'a' + 10);
            change.boot_id           = (change.boot_id << 4U) | digit;
        }
    }
    *p_change = change;
    return 0;
}

/** @brief Parse a bounded request line and at most sixteen header fields. */
static int parse (nethttp_t * p_http)
{
    char *   p_line    = strstr(p_http->headers, "\r\n");
    char *   p_path    = NULL;
    char *   p_version = NULL;
    char *   p_auth    = NULL;
    bool     b_host    = false;
    bool     b_length  = false;
    size_t   count     = 0U;
    uint32_t fields    = 0U;
    if ((NULL == p_line) || ((size_t)(p_line - p_http->headers) > 256U))
    {
        return 400;
    }
    *p_line = '\0';
    p_path  = strchr(p_http->headers, ' ');
    if (NULL == p_path)
    {
        return 400;
    }
    *p_path++ = '\0';
    p_version = strchr(p_path, ' ');
    if (NULL == p_version)
    {
        return 400;
    }
    *p_version++ = '\0';
    if ((0 != strcmp(p_version, "HTTP/1.1")) || (96U < strlen(p_path)))
    {
        return 400;
    }
    p_line += 2U;
    while ('\0' != *p_line)
    {
        char * p_end   = strstr(p_line, "\r\n");
        char * p_value = strchr(p_line, ':');
        if (p_end == p_line)
        {
            break;
        }
        ++count;
        if ((NULL == p_end) || (NULL == p_value) || (p_value >= p_end) ||
            (256U < (size_t)(p_end - p_line)) || (16U < count))
        {
            return 400;
        }
        *p_end     = '\0';
        *p_value++ = '\0';
        if (!field_name(p_line))
        {
            return 400;
        }
        while (' ' == *p_value)
        {
            ++p_value;
        }
        if (same(p_line, "host"))
        {
            if ((b_host) || ('\0' == *p_value))
            {
                return 400;
            }
            b_host = true;
        }
        else if (same(p_line, "authorization"))
        {
            if (NULL != p_auth)
            {
                return 400;
            }
            p_auth = p_value;
        }
        else if (same(p_line, "content-length"))
        {
            if ((b_length) || (!decimal(p_value, 0U, &p_http->content_size)))
            {
                return 400;
            }
            b_length = true;
        }
        else if (same(p_line, "transfer-encoding"))
        {
            return 400;
        }
        else if (same(p_line, "expect"))
        {
            return 417;
        }
        else if (same(p_line, "content-encoding"))
        {
            return 415;
        }
        else
        {
            const int field = transaction_field(p_line);
            if (0 <= field)
            {
                const uint32_t bit = UINT32_C(1) << (unsigned int)field;
                if (0U != (fields & bit))
                {
                    return 400;
                }
                fields |= bit;
            }
        }
        p_line = p_end + 2U;
    }
    if (!b_host)
    {
        return 400;
    }
    if ((NULL == p_auth) || (!authorized(p_http, p_auth)))
    {
        return 401;
    }
    if ((0 == strcmp(p_http->headers, "POST")) &&
        (0 == strcmp(p_path, "/v1/configuration")))
    {
        if (8192U < p_http->content_size)
        {
            return 413;
        }
        const char * p_type = header_value(p_http, "content-type");
        if ((NULL == p_type) || (0 != strcmp(p_type, "application/json")))
        {
            return 415;
        }
        p_http->route           = NETHTTP_CONFIG_APPLY;
        nethttp_change_t change = {0};
        return ((b_length) && (0U != p_http->content_size) &&
                (0 == nethttp_change(p_http, &change)))
                   ? 200
                   : 400;
    }
    if (0 != strcmp(p_http->headers, "GET"))
    {
        return 405;
    }
    if (0U != p_http->content_size)
    {
        return 400;
    }
    const int result = route(p_http, p_path);
    if ((200 == result) && (NETHTTP_CONFIG_RESULT == p_http->route))
    {
        nethttp_change_t change = {0};
        return (0 == nethttp_change(p_http, &change)) ? 200 : 400;
    }
    return result;
}

/** @brief Compare header names without process-global locale state. */
static bool same (char const * p_left, char const * p_right)
{
    size_t index = 0U;
    while (('\0' != p_left[index]) && ('\0' != p_right[index]))
    {
        unsigned char character = (unsigned char)p_left[index];
        if (('A' <= character) && ('Z' >= character))
        {
            character += 'a' - 'A';
        }
        if (character != (unsigned char)p_right[index])
        {
            return false;
        }
        ++index;
    }
    return p_left[index] == p_right[index];
}

/** @brief Compare all secret bytes without an early secret-prefix exit. */
static bool authorized (nethttp_t const * p_http, char const * p_value)
{
    uint8_t difference = 0U;
    size_t  size       = 0U;
    if (0 != strncmp(p_value, "Bearer ", 7U))
    {
        return false;
    }
    p_value += 7U;
    size = strlen(p_value);
    for (size_t index = 0U; index < UTHOME_HTTP_SECRET; ++index)
    {
        const uint8_t actual = (index < size) ? (uint8_t)p_value[index] : 0U;
        difference |= actual ^ p_http->secret[index];
    }
    return (size == p_http->secret_size) && (0U == difference);
}

/** @brief Select only implemented read operations and exact source indices. */
static int route (nethttp_t * p_http, char const * p_path)
{
    if (0 == strcmp(p_path, "/v1/identity"))
    {
        p_http->route = NETHTTP_IDENTITY;
    }
    else if (0 == strcmp(p_path, "/v1/status"))
    {
        p_http->route = NETHTTP_STATUS;
    }
    else if (0 == strcmp(p_path, "/v1/configuration"))
    {
        p_http->route = NETHTTP_CONFIG_STATE;
    }
    else if (0 == strcmp(p_path, "/v1/configuration/result"))
    {
        p_http->route = NETHTTP_CONFIG_RESULT;
    }
    else if ((0 == strncmp(p_path, "/v1/telemetry?source=", 21U)) &&
             ('0' <= p_path[21]) && ('7' >= p_path[21]) && ('\0' == p_path[22]))
    {
        p_http->route  = NETHTTP_TELEMETRY;
        p_http->source = (uint16_t)(p_path[21] - '0');
    }
    else
    {
        return 404;
    }
    return 200;
}

/** @brief Require HTTP token field names, excluding folded/ambiguous lines. */
static bool field_name (char const * p_name)
{
    if ('\0' == *p_name)
    {
        return false;
    }
    for (size_t index = 0U; '\0' != p_name[index]; ++index)
    {
        const unsigned char character = (unsigned char)p_name[index];
        if ((!((('a' <= character) && ('z' >= character)) ||
               (('A' <= character) && ('Z' >= character)) ||
               (('0' <= character) && ('9' >= character)))) &&
            (NULL == strchr("!#$%&'*+-.^_`|~", character)))
        {
            return false;
        }
    }
    return true;
}

/** @brief Parsed lines retain bounded names/values separated by NUL and LF. */
static const char * header_value (const nethttp_t * p_http, const char * p_name)
{
    const char * p_end  = p_http->headers + p_http->used;
    const char * p_line = memchr(p_http->headers, '\n', p_http->used);
    if (NULL == p_line)
    {
        return NULL;
    }
    ++p_line;
    while (p_line < p_end)
    {
        const char * p_next = memchr(p_line, '\n', (size_t)(p_end - p_line));
        if ((NULL == p_next) || ('\r' == *p_line) || ('\0' == *p_line))
        {
            break;
        }
        if (same(p_line, p_name))
        {
            const char * p_value = p_line + strlen(p_line) + 1U;
            while (' ' == *p_value)
            {
                ++p_value;
            }
            return p_value;
        }
        p_line = p_next + 1U;
    }
    return NULL;
}

/** @brief Reject signs, whitespace, leading zeroes and uint32 overflow. */
static bool decimal (const char * p_text, uint32_t minimum, uint32_t * p_value)
{
    uint32_t value = 0U;
    if ((NULL == p_text) || ('\0' == p_text[0]) ||
        (('0' == p_text[0]) && ('\0' != p_text[1])))
    {
        return false;
    }
    for (size_t index = 0U; '\0' != p_text[index]; ++index)
    {
        const unsigned char character = (unsigned char)p_text[index];
        if (('0' > character) || ('9' < character))
        {
            return false;
        }
        const uint32_t digit = character - '0';
        if (value > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }
        value = value * 10U + digit;
    }
    *p_value = value;
    return value >= minimum;
}

/** @brief Transaction-affecting duplicates are never silently merged. */
static int transaction_field (const char * p_name)
{
    if (same(p_name, "x-request-id"))
    {
        return 0;
    }
    if (same(p_name, "x-revision"))
    {
        return 1;
    }
    if (same(p_name, "x-expected-revision"))
    {
        return 2;
    }
    if (same(p_name, "x-expected-boot-id"))
    {
        return 3;
    }
    if (same(p_name, "x-request-token"))
    {
        return 4;
    }
    if (same(p_name, "content-type"))
    {
        return 5;
    }
    return -1;
}

/* End of file. */

