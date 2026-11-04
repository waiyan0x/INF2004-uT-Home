/**
 * @file c3https.h
 * @brief Two-client nonblocking ESP-TLS server driven by the app owner.
 */
#ifndef UTHOME_C3HTTPS_H
#define UTHOME_C3HTTPS_H
#include "netread.h"
#include "cfgmux.h"

typedef struct
{
    const uint8_t * p_certificate;
    const uint8_t * p_key;
    const uint8_t * p_token;
    size_t          certificate_size;
    size_t          key_size;
    size_t          token_size;
    uint16_t        port;
    cfgmux_t *      p_mux;
} c3https_config_t;

/* One owner, one startup attempt per boot. PEM buffers include their NUL and
 * remain alive until reboot. Uses audited SDK TLS allocations, not a fixed-
 * allocation qualification. No default/plaintext certificate or credential.
 */
int c3https_start (const c3https_config_t * p_config);
/* No blocking wait/retry loops. One accept and one operation/client per step.
 * Metadata/capture must be serialized with configuration by the app owner.
 * Offline closes clients/listener; online retries listening at most once/s.
 * TLS handshake/reply deadlines 10 s; HTTP headers/body share 10 s.
 * Return 1 after a configuration POST: refresh owner metadata before any
 * subsequent sample capture. The app gates USB while cfgmux is leased.
 */
int c3https_step (uint64_t               now_ms,
                  bool                   b_online,
                  const netread_info_t * p_info,
                  netread_capture_t *    p_capture,
                  void *                 p_context);
void c3https_stop (void);
#endif

/* End of file. */

