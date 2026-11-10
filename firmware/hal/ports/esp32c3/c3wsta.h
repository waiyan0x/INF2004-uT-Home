/**
 * @file c3wsta.h
 * @brief RAM-only Wi-Fi station association and finite reconnect attempts.
 */
#ifndef UTHOME_C3WSTA_H
#define UTHOME_C3WSTA_H
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    C3WSTA_STAGE_NONE = 0,
    C3WSTA_STAGE_NETIF,
    C3WSTA_STAGE_EVENT_LOOP,
    C3WSTA_STAGE_INTERFACE,
    C3WSTA_STAGE_WIFI_EVENT,
    C3WSTA_STAGE_GOT_IP,
    C3WSTA_STAGE_LOST_IP,
    C3WSTA_STAGE_INITIALIZE,
    C3WSTA_STAGE_STORAGE,
    C3WSTA_STAGE_MODE,
    C3WSTA_STAGE_CONFIGURE,
    C3WSTA_STAGE_START,
    C3WSTA_STAGE_READY,
    C3WSTA_STAGE_CONNECT,
    C3WSTA_STAGE_DISCONNECT,
    C3WSTA_STAGE_TX_POWER
} c3wsta_stage_t;

typedef struct
{
    uint8_t  stage;
    uint8_t  retry_state;
    uint8_t  flags;
    uint16_t disconnect_reason;
    int32_t  sdk_error;
    uint8_t  address[4];
    uint64_t deadline_ms;
} c3wsta_status_t;

/* One startup attempt, one owner; SSID <=32 and password 8..63 bytes.
 * tx_power_quarter_dbm: zero preserves the SDK default; 8..84 requests a cap
 * in SDK quarter-dBm units before each connection attempt. No NVS writes,
 * scan dump or credential logs. SDK allocation remains audited.
 */
int c3wsta_start (const char * p_ssid,
                  const char * p_password,
                  int32_t      tx_power_quarter_dbm);
int c3wsta_step (uint64_t now_ms);
bool c3wsta_online (void);
/* Read-only owner snapshot; no SDK operation, retry, allocation or reset.
 * Flags: attempted/start succeeded/station started/IP/cancelling/fault in
 * bits 0..5. Address is IPv4 network byte order, zero when IP is absent.
 * Stage/error retain the latest SDK operation. Deadline is retry monotonic ms.
 */
int c3wsta_status (c3wsta_status_t * p_status);
#endif

/* End of file. */

