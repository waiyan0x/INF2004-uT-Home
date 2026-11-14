/**
 * @file c3tls.c
 * @brief Typed ESP-TLS leases with bounded exhaustion and complete reuse.
 */
#include "c3tls.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "esp_tls_private.h"
#include <stdbool.h>
#include <string.h>

#define UTHOME_TLS_CONNECTIONS 3U
#define UTHOME_TLS_NAME_BYTES  254U

typedef struct
{
    esp_tls_last_error_t last;
    int                  socket_error;
    bool                 b_used;
} c3tls_error_t;

static esp_tls_t     g_contexts[UTHOME_TLS_CONNECTIONS] = {0};
static bool          gb_used[UTHOME_TLS_CONNECTIONS]    = {false};
static c3tls_error_t g_errors[UTHOME_TLS_CONNECTIONS]   = {0};
static char          g_name[UTHOME_TLS_NAME_BYTES]      = {0};
static bool          gb_name_used                       = false;

static int * error_field (esp_tls_error_handle_t h_error, uint32_t type);

/** @brief Lease only the actual SDK context type, never arbitrary byte storage.
 */
void * c3tls_alloc (size_t count, size_t size)
{
    if ((1U != count) || (sizeof(g_contexts[0]) != size))
    {
        return NULL;
    }
    for (size_t index = 0U; index < UTHOME_TLS_CONNECTIONS; ++index)
    {
        if (!gb_used[index])
        {
            memset(&g_contexts[index], 0, sizeof(g_contexts[index]));
            gb_used[index] = true;
            return &g_contexts[index];
        }
    }
    return NULL;
}

/** @brief Release recognized storage after SDK cleanup; never call libc free.
 */
void c3tls_free (void * p_memory)
{
    if (p_memory == g_name)
    {
        memset(g_name, 0, sizeof(g_name));
        gb_name_used = false;
        return;
    }
    for (size_t index = 0U; index < UTHOME_TLS_CONNECTIONS; ++index)
    {
        if (p_memory == &g_contexts[index])
        {
            memset(&g_contexts[index], 0, sizeof(g_contexts[index]));
            gb_used[index] = false;
            return;
        }
    }
    /* NULL and foreign pointers do not belong to either fixed store. */
}

/** @brief The SDK frees this scratch copy before returning to the owner. */
char * c3tls_dup (const char * p_text, size_t size)
{
    if ((NULL == p_text) || (size >= sizeof(g_name)) || gb_name_used)
    {
        return NULL;
    }
    size_t length = 0U;
    while ((length < size) && ('\0' != p_text[length]))
    {
        ++length;
    }
    memcpy(g_name, p_text, length);
    g_name[length] = '\0';
    gb_name_used   = true;
    return g_name;
}

/** @brief Keep the SDK's public error type as a real, aligned subobject. */
esp_tls_error_handle_t c3tls_error_create (void)
{
    for (size_t index = 0U; index < UTHOME_TLS_CONNECTIONS; ++index)
    {
        if (!g_errors[index].b_used)
        {
            g_errors[index] = (c3tls_error_t){.b_used = true};
            return &g_errors[index].last;
        }
    }
    return NULL;
}

/** @brief Invalidate an error handle after all its SDK users have stopped. */
void c3tls_error_destroy (esp_tls_error_handle_t h_error)
{
    for (size_t index = 0U; index < UTHOME_TLS_CONNECTIONS; ++index)
    {
        if (h_error == &g_errors[index].last)
        {
            g_errors[index] = (c3tls_error_t){0};
            return;
        }
    }
}

/** @brief Preserve zero and signed SDK errors; ignore unsupported error types.
 */
void c3tls_error_capture (esp_tls_error_handle_t h_error,
                          uint32_t               type,
                          int                    code)
{
    int * p_field = error_field(h_error, type);
    if (NULL != p_field)
    {
        *p_field = code;
    }
}

/** @brief Read and clear only the requested live error category. */
esp_err_t c3tls_error_get (esp_tls_error_handle_t h_error,
                           esp_tls_error_type_t   type,
                           int *                  p_code)
{
    /* Negative/unknown enum values become unsupported unsigned categories. */
    int * p_field = error_field(h_error, (uint32_t)type);
    if ((NULL == p_field) || (NULL == p_code))
    {
        return ESP_ERR_INVALID_ARG;
    }
    *p_code  = *p_field;
    *p_field = 0;
    return ESP_OK;
}

/** @brief Resolve by pointer identity; never subtract or dereference a
 * stranger.
 */
static int * error_field (esp_tls_error_handle_t h_error, uint32_t type)
{
    c3tls_error_t * p_record = NULL;
    for (size_t index = 0U; index < UTHOME_TLS_CONNECTIONS; ++index)
    {
        if ((h_error == &g_errors[index].last) && (g_errors[index].b_used))
        {
            p_record = &g_errors[index];
            break;
        }
    }
    if (NULL == p_record)
    {
        return NULL;
    }
    switch (type)
    {
    case ESP_TLS_ERR_TYPE_SYSTEM:
        return &p_record->socket_error;
    case ESP_TLS_ERR_TYPE_ESP:
        return &p_record->last.last_error;
    case ESP_TLS_ERR_TYPE_MBEDTLS:
    case ESP_TLS_ERR_TYPE_CUSTOM_STACK:
        return &p_record->last.esp_tls_error_code;
    case ESP_TLS_ERR_TYPE_MBEDTLS_CERT_FLAGS:
    case ESP_TLS_ERR_TYPE_CUSTOM_STACK_CERT_FLAGS:
        return &p_record->last.esp_tls_flags;
    default:
        return NULL;
    }
}
#endif

/* End of file. */

