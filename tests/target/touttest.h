/**
 * @file touttest.h
 * @brief Host SDK-call double; no encryption or network is simulated.
 */
#ifndef UTHOME_TOUTTEST_H
#define UTHOME_TOUTTEST_H
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t words[21];
} touttest_transform_t;

typedef struct
{
    uint32_t words[36];
} touttest_session_t;

typedef struct
{
    uint32_t words[612];
} touttest_handshake_t;

typedef struct
{
    uint32_t failure;
} touttest_config_t;

typedef struct
{
    unsigned char *           p_in_buf;
    unsigned char *           p_out_buf;
    char *                    p_hostname;
    uint32_t                  flags;
    const touttest_config_t * p_config;
    touttest_transform_t *    p_transform_new;
    touttest_session_t *      p_session_new;
    touttest_handshake_t *    p_handshake;
    touttest_transform_t *    p_transform;
    touttest_session_t *      p_session;
    int                       state;
    int                       renegotiation;
} touttest_context_t;

int touttest_setup (touttest_context_t *      p_ssl,
                    const touttest_config_t * p_config);
int touttest_name (touttest_context_t * p_ssl, const char * p_hostname);
int touttest_start (touttest_context_t * p_ssl);
int touttest_renegotiate (touttest_context_t * p_ssl);
void touttest_promote (touttest_context_t * p_ssl);
void * touttest_alloc (size_t count, size_t size);
void touttest_free (void * p_memory);
void touttest_zeroize (void * p_memory, size_t size);
void touttest_wipe (void * p_memory, size_t size);
uint32_t touttest_count (uint32_t kind);
#endif

/* End of file. */

