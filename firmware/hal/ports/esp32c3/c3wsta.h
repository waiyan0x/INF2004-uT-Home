/**
 * @file c3wsta.h
 * @brief RAM-only Wi-Fi station association and finite reconnect attempts.
 */
#ifndef UTHOME_C3WSTA_H
#define UTHOME_C3WSTA_H
#include <stdbool.h>
#include <stdint.h>
/* One startup attempt, one owner; SSID <=32 and password 8..63 bytes.
 * No NVS writes, scan dump or credential logs. SDK allocation remains audited.
 */
int c3wsta_start (const char * p_ssid, const char * p_password);
int c3wsta_step (uint64_t now_ms);
bool c3wsta_online (void);
#endif

/* End of file. */

