"""Compile actual ESP wrappers against explicit SDK I/O doubles.

This exercises wrapper ownership/error paths; it does not emulate RF, TLS
cryptography, target ABI, scheduling or static-memory qualification.
"""
import ctypes as c
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class WifiStatus(c.Structure):
    _fields_=[('stage',c.c_uint8),('retry_state',c.c_uint8),('flags',c.c_uint8),
              ('disconnect_reason',c.c_uint16),('sdk_error',c.c_int32),
              ('address',c.c_uint8*4),('deadline_ms',c.c_uint64)]

SDK_HEADER=r"""
#ifndef TEST_ESP_SDK_H
#define TEST_ESP_SDK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define F_SETFL 4
#define O_NONBLOCK 0x800
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_WIFI_NOT_CONNECT 12290
#define ESP_TLS_ERR_SSL_WANT_READ -26880
#define ESP_TLS_ERR_SSL_WANT_WRITE -26752
#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define IPPROTO_TCP 6
#define INADDR_ANY 0U
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
struct in_addr { uint32_t s_addr; };
struct sockaddr_in { short sin_family; uint16_t sin_port; struct in_addr sin_addr; };
/* Foreign SDK names and private fields used by the owned MQTT adapter. */
#define ESP_TLS_CONNECTING 1
#define FD_ZERO c3fake_fd_zero
#define FD_SET c3fake_fd_set
void c3fake_fd_zero (uint64_t * p_set);
void c3fake_fd_set (int h_socket, uint64_t * p_set);
void c3fake_connect_timeouts (uint32_t count);
uint32_t c3fake_tls_polls (void);
typedef struct { int socket; int conn_state; int sockfd;
 uint64_t rset; uint64_t wset; } esp_tls_t;
typedef struct { const unsigned char *cacert_buf; unsigned int cacert_bytes;
 const char *common_name; bool non_block; int timeout_ms; } esp_tls_cfg_t;
int esp_tls_conn_new_async(const char *,int,int,const esp_tls_cfg_t *,esp_tls_t *);
int esp_tls_conn_destroy(esp_tls_t *);
int inet_pton(int,const char *,void *);
esp_tls_t *esp_tls_init(void);
ssize_t esp_tls_conn_read(esp_tls_t *tls,void *data,size_t size);
ssize_t esp_tls_conn_write(esp_tls_t *tls,const void *data,size_t size);
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void event_handler_t(void *,esp_event_base_t,int32_t,void *);
extern esp_event_base_t WIFI_EVENT;
extern esp_event_base_t IP_EVENT;
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_STA_STOP 3
#define IP_EVENT_STA_GOT_IP 4
#define IP_EVENT_STA_LOST_IP 5
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 1
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_IF_STA 0
typedef struct { bool nvs_enable; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){true})
typedef struct { struct { uint8_t ssid[32]; uint8_t password[64];
 struct { int authmode; } threshold; } sta; } wifi_config_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { struct { struct { uint32_t addr; } ip; } ip_info; } ip_event_got_ip_t;
int esp_netif_init(void);
void *esp_netif_create_default_wifi_sta(void);
int esp_event_loop_create_default(void);
int esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
 event_handler_t *handler,void *arg,esp_event_handler_instance_t *instance);
int esp_wifi_init(const wifi_init_config_t *config);
int esp_wifi_set_storage(int storage);
int esp_wifi_set_mode(int mode);
int esp_wifi_set_config(int interface_id,const wifi_config_t *config);
int esp_wifi_start(void);
int esp_wifi_set_max_tx_power(int8_t power);
int esp_wifi_connect(void);
int esp_wifi_disconnect(void);
uint32_t esp_random(void);
#define ESP_MAC_WIFI_STA 0
int esp_read_mac(uint8_t *address,int type);
void esp_fill_random(void *data,size_t size);
#define BLE_HS_CONN_HANDLE_NONE 65535U
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_NOTIFY 16
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_SUBSCRIBE 2
#define BLE_GAP_EVENT_MTU 3
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER -1
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) { {128}, {__VA_ARGS__} }
struct os_mbuf { uint8_t data[128]; uint16_t size; };
struct ble_gatt_access_ctxt;
typedef int access_t(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_chr_def { const ble_uuid_t *uuid; access_t *access_cb;
 uint16_t flags; uint16_t *val_handle; };
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om;
 const struct ble_gatt_chr_def *chr; };
struct ble_gatt_svc_def { int type; const ble_uuid_t *uuid;
 const struct ble_gatt_chr_def *characteristics; };
struct ble_gap_event { int type; struct { int status; uint16_t conn_handle; } connect;
 struct { uint16_t attr_handle; unsigned int cur_notify; } subscribe; };
struct ble_hs_adv_fields { uint8_t flags; uint8_t *name; uint8_t name_len;
 uint8_t name_is_complete; };
struct ble_gap_adv_params { int conn_mode; int disc_mode; };
typedef int gap_t(struct ble_gap_event *,void *);
typedef void host_t(void *);
typedef void sync_t(void);
typedef void reset_t(int);
struct fake_hs_config { sync_t *sync_cb; reset_t *reset_cb; };
extern struct fake_hs_config ble_hs_cfg;
int nimble_port_init(void);
void nimble_port_freertos_init(host_t *entry);
void nimble_port_run(void);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_svc_gap_device_name_set(const char *name);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *services);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *services);
int ble_uuid_cmp(const ble_uuid_t *left,const ble_uuid_t *right);
int os_mbuf_append(struct os_mbuf *buffer,const void *data,uint16_t size);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data,uint16_t size);
int ble_gatts_notify_custom(uint16_t connection,uint16_t attribute,struct os_mbuf *buffer);
int ble_hs_util_ensure_addr(int prefer_random);
int ble_hs_id_infer_auto(int privacy,uint8_t *type);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields);
int ble_gap_adv_start(uint8_t type,const void *address,int duration,
 const struct ble_gap_adv_params *params,gap_t *handler,void *arg);
uint16_t ble_att_mtu(uint16_t connection);
#endif
"""

FAKE_SOURCE=r"""
#include "sdk.h"
#include "c3mqtt.h"
#include "c3wsta.h"
#include "c3gsvc.h"
#include <string.h>
esp_event_base_t WIFI_EVENT="wifi";
esp_event_base_t IP_EVENT="ip";
struct fake_hs_config ble_hs_cfg={0};
static event_handler_t *g_event;
static gap_t *g_gap;
static const struct ble_gatt_svc_def *g_services;
static int g_deleted;
static esp_tls_t g_tls[1];
static bool g_allocated[1];
static int g_handshake;
static uint32_t g_connect_timeouts = 0U;
static uint32_t g_tls_polls = 0U;
static int g_write_limit=17;
static char g_input[64][1100];
static size_t g_input_size[64];
static uint8_t g_output[64][4096];
static size_t g_output_size[64];
static int g_connects;
static int g_disconnects;
static int g_connect_error;
static int g_disconnect_error;
static int g_wifi_fail_stage;
static int g_wifi_setup_calls;
static int g_power_calls = 0;
static int g_power_error = 0;
static int32_t g_wifi_operations[32] = {0};
static uint32_t g_wifi_operation_count = 0U;
static uint16_t g_mtu=23;
static int g_advertised;
static int g_notifications;
static int g_notify_error;
static struct os_mbuf g_buffer;
static uint8_t g_notification[128];
static size_t g_notification_size;
uint32_t c3bport_enter(void) { return 0U; }
void c3bport_leave(uint32_t saved) { (void)saved; }
void c3bport_exit(void) { }
esp_tls_t * esp_tls_init (void)
{
    for (size_t index = 0U; index < (sizeof(g_tls) / sizeof(g_tls[0])); ++index)
    {
        if (!g_allocated[index])
        {
            g_allocated[index] = true;
            memset(&g_tls[index], 0, sizeof(g_tls[index]));
            return &g_tls[index];
        }
    }
    return NULL;
}
int inet_pton(int family,const char *text,void *out)
{ (void)family;(void)out;return strcmp(text,"127.0.0.1")==0; }
/** Clear a foreign socket-set double. */
void c3fake_fd_zero (uint64_t * p_set)
{
    *p_set = 0U;
}
/** The test socket is always within the double's 64-bit set. */
void c3fake_fd_set (int h_socket, uint64_t * p_set)
{
    *p_set |= UINT64_C(1) << h_socket;
}
/** Supply readiness timeouts for the next connect attempt. */
void c3fake_connect_timeouts (uint32_t count)
{
    g_connect_timeouts = count;
}
/** Read the number of simulated readiness polls. */
uint32_t c3fake_tls_polls (void)
{
    return g_tls_polls;
}
/** Model the pinned SDK's destructive select sets between owner calls. */
int esp_tls_conn_new_async (const char * p_host, int size, int port,
                           const esp_tls_cfg_t * p_config, esp_tls_t * p_tls)
{
    /* Address/port parsing is outside this SDK readiness double. */
    (void)p_host;
    (void)size;
    (void)port;
    if ((!p_config->non_block) || (NULL == p_config->cacert_buf) ||
        (NULL == p_config->common_name))
    {
        return -1;
    }
    if (0 == p_tls->conn_state)
    {
        p_tls->conn_state = ESP_TLS_CONNECTING;
        p_tls->rset = UINT64_C(1);
        p_tls->wset = UINT64_C(1);
    }
    ++g_tls_polls;
    const bool b_watched = 0U != ((p_tls->rset | p_tls->wset) & UINT64_C(1));
    p_tls->rset = 0U;
    p_tls->wset = 0U;
    if (0U != g_connect_timeouts)
    {
        --g_connect_timeouts;
        return 0;
    }
    return b_watched ? g_handshake : 0;
}
int esp_tls_conn_destroy(esp_tls_t *tls)
{ if(tls==&g_tls[0]) { g_allocated[0]=false; } ++g_deleted;return 0; }
ssize_t esp_tls_conn_read(esp_tls_t *tls,void *data,size_t size)
{ int fd=tls->socket;size_t n=g_input_size[fd];
  if(!n) { return ESP_TLS_ERR_SSL_WANT_READ; }
  if(n>size) { n=size; } memcpy(data,g_input[fd],n);
  memmove(g_input[fd],g_input[fd]+n,g_input_size[fd]-n);g_input_size[fd]-=n;return (ssize_t)n; }
ssize_t esp_tls_conn_write(esp_tls_t *tls,const void *data,size_t size)
{ int fd=tls->socket;if(g_write_limit<0) { return g_write_limit; }
  size_t n=size;if(n>(size_t)g_write_limit) { n=(size_t)g_write_limit; }
  if(g_output_size[fd]+n>4096) { return -1; }
  memcpy(g_output[fd]+g_output_size[fd],data,n);g_output_size[fd]+=n;return (ssize_t)n; }
static int wifi_stage(int stage)
{ ++g_wifi_setup_calls;return stage==g_wifi_fail_stage?0x4321:0; }
int esp_netif_init(void) { return wifi_stage(1); }
void *esp_netif_create_default_wifi_sta(void) { return wifi_stage(3)?NULL:&g_connects; }
int esp_event_loop_create_default(void) { return wifi_stage(2); }
int esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
 event_handler_t *handler,void *arg,esp_event_handler_instance_t *instance)
{ (void)arg;g_event=handler;if(instance) { *instance=&g_event; }
 return wifi_stage(base==WIFI_EVENT?4:(id==IP_EVENT_STA_GOT_IP?5:6)); }
int esp_wifi_init(const wifi_init_config_t *config) { return config->nvs_enable?-1:wifi_stage(7); }
int esp_wifi_set_storage(int storage) { return storage?storage:wifi_stage(8); }
int esp_wifi_set_mode(int mode) { return mode==WIFI_MODE_STA?wifi_stage(9):-1; }
int esp_wifi_set_config(int interface_id,const wifi_config_t *config)
{ (void)interface_id;return config->sta.threshold.authmode==WIFI_AUTH_WPA2_PSK?wifi_stage(10):-1; }
static void wifi_operation (int32_t value)
{
    if (32U > g_wifi_operation_count)
    {
        g_wifi_operations[g_wifi_operation_count] = value;
        ++g_wifi_operation_count;
    }
}
int esp_wifi_start(void) { wifi_operation(11);return wifi_stage(11); }
int esp_wifi_set_max_tx_power (int8_t power)
{
    ++g_power_calls;
    wifi_operation(1000 + power);
    return g_power_error;
}
int esp_wifi_connect(void) { wifi_operation(13);++g_connects;return g_connect_error; }
int esp_wifi_disconnect(void) { ++g_disconnects;return g_disconnect_error; }
uint32_t esp_random(void) { return 0U; }
int esp_read_mac(uint8_t *address,int type)
{ (void)type;for(int i=0;i<6;++i) { address[i]=(uint8_t)i; } return 0; }
void esp_fill_random(void *data,size_t size) { memset(data,1,size); }
int nimble_port_init(void) { return 0; }
void nimble_port_freertos_init(host_t *entry) { (void)entry;ble_hs_cfg.sync_cb(); }
void nimble_port_run(void) { }
void ble_svc_gap_init(void) { }
void ble_svc_gatt_init(void) { }
int ble_svc_gap_device_name_set(const char *name) { return strcmp(name,"UTHOME-SENSOR"); }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *services) { (void)services;return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *services)
{ g_services=services;*services[0].characteristics[2].val_handle=33;return 0; }
int ble_uuid_cmp(const ble_uuid_t *left,const ble_uuid_t *right)
{ return memcmp(left,right,sizeof(ble_uuid128_t)); }
int os_mbuf_append(struct os_mbuf *buffer,const void *data,uint16_t size)
{ if(size>128) { return -1; } memcpy(buffer->data,data,size);buffer->size=size;return 0; }
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data,uint16_t size)
{ if(size>128) { return NULL; } memcpy(g_buffer.data,data,size);g_buffer.size=size;return &g_buffer; }
int ble_gatts_notify_custom(uint16_t connection,uint16_t attribute,struct os_mbuf *buffer)
{ (void)connection;(void)attribute;if(g_notify_error) { return g_notify_error; }
  memcpy(g_notification,buffer->data,buffer->size);g_notification_size=buffer->size;
  ++g_notifications;return 0; }
int ble_hs_util_ensure_addr(int prefer_random) { (void)prefer_random;return 0; }
int ble_hs_id_infer_auto(int privacy,uint8_t *type) { (void)privacy;*type=0;return 0; }
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields) { (void)fields;return 0; }
int ble_gap_adv_start(uint8_t type,const void *address,int duration,
 const struct ble_gap_adv_params *params,gap_t *handler,void *arg)
{ (void)type;(void)address;(void)duration;(void)params;(void)arg;g_gap=handler;++g_advertised;return 0; }
uint16_t ble_att_mtu(uint16_t connection) { (void)connection;return g_mtu; }
static netread_info_t g_info={.meta={.boot_id=1,.active_revision=12,.generation=4},
 .saved_revision=11,.runtime_state=1,.b_durable_known=true,.b_wifi_online=true,.node_id="test-node"};
/* Per-slot capture result for MQTT tests: 0 copies the source, 1 absent,
 * negative busy/failure. Defaults keep the original all-present fixture. */
static int g_capture_result[8];
static int g_capture_failures[8];
static int g_captures;
#include "uthome/txjob.h"
static int capture(void *context,uint16_t source,sampcache_entry_t *entry,uint64_t *now)
{ ++g_captures;
  if(source<8 && g_capture_failures[source]>0) { --g_capture_failures[source];memset(entry,0,sizeof(*entry));return -1; }
  if(source<8 && g_capture_result[source]!=0) { memset(entry,0,sizeof(*entry));return g_capture_result[source]; }
  (void)context;memset(entry,0,sizeof(*entry));entry->source.type=1;
  entry->source.source_index=source;entry->source.stale_ms=1000;
  memcpy(entry->source.name,"environment",12);entry->b_expected=true;entry->b_seen=true;
  entry->header.status=1;entry->header.timestamp_ms=100;
  entry->value.environment.pressure_pa=100800;entry->value.environment.temperature_c=26;
  *now=150;return 0; }
void c3fake_input(int fd,const char *data,size_t size)
{ if(size<=1100) { memcpy(g_input[fd],data,size);g_input_size[fd]=size; } }
size_t c3fake_output(int fd,void *output)
{ memcpy(output,g_output[fd],g_output_size[fd]);return g_output_size[fd]; }
int c3fake_deleted(void) { return g_deleted; }
void c3fake_handshake(int result) { g_handshake=result; }
void c3fake_write_limit(int size) { g_write_limit=size; }
void c3fake_event(int identifier)
{ g_event(NULL,identifier<=3?WIFI_EVENT:IP_EVENT,identifier,NULL); }
void c3fake_wifi_failure(int stage) { g_wifi_fail_stage=stage; }
void c3fake_power_error (int error) { g_power_error = error; }
int c3fake_power_calls (void) { return g_power_calls; }
int32_t c3fake_wifi_operation (uint32_t index)
{
    return (g_wifi_operation_count > index) ? g_wifi_operations[index] : -1;
}
int c3fake_wifi_setup_calls(void) { return g_wifi_setup_calls; }
void c3fake_wifi_errors(int connect_error,int disconnect_error)
{ g_connect_error=connect_error;g_disconnect_error=disconnect_error; }
void c3fake_wifi_ip(uint8_t first,uint8_t second,uint8_t third,uint8_t fourth)
{ ip_event_got_ip_t data={0};uint8_t address[4]={first,second,third,fourth};
 memcpy(&data.ip_info.ip.addr,address,sizeof(address));
 g_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&data); }
void c3fake_wifi_disconnected(uint8_t reason)
{ wifi_event_sta_disconnected_t data={reason};g_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&data); }
int c3fake_connects(void) { return g_connects; }
int c3fake_disconnects(void) { return g_disconnects; }
int c3fake_mqtt_start(void)
{ static const uint8_t ca[]="certificate";
 const c3mqtt_config_t cfg={.p_address="127.0.0.1",.p_server_name="broker.test",
 .p_user="user",.p_password="password",.p_ca=ca,.ca_size=sizeof(ca),.port=8883};
 return c3mqtt_start(&cfg,"node"); }
int c3fake_mqtt_step(uint64_t now,int online)
{ return c3mqtt_step(now,online!=0,&g_info,capture,NULL); }
void c3fake_capture_result(int slot,int result) { if(slot>=0 && slot<8) { g_capture_result[slot]=result; } }
void c3fake_capture_failures(int slot,int count) { if(slot>=0 && slot<8) { g_capture_failures[slot]=count; } }
int c3fake_captures(void) { return g_captures; }
void c3fake_generation(uint32_t generation) { g_info.meta.generation=generation; }
size_t c3fake_take_output(int fd,void *output)
{ size_t size=g_output_size[fd];memcpy(output,g_output[fd],size);g_output_size[fd]=0;return size; }
int c3fake_ble_start(void) { return c3gsvc_start(&g_info); }
int c3fake_ble_step(uint64_t now) { return c3gsvc_step(now,&g_info,capture,NULL); }
void c3fake_gap(int kind,int value)
{ struct ble_gap_event event={0};event.type=kind;event.connect.conn_handle=7;
  event.subscribe.attr_handle=33;event.subscribe.cur_notify=(unsigned int)value;
  g_gap(&event,NULL); }
int c3fake_read(int characteristic,int write,void *output)
{ struct os_mbuf buffer={0};struct ble_gatt_access_ctxt context={0};
  const struct ble_gatt_chr_def *chr=&g_services[0].characteristics[characteristic];
  context.op=write?2:BLE_GATT_ACCESS_OP_READ_CHR;context.om=&buffer;context.chr=chr;
  int result=chr->access_cb(7,33,&context,NULL);if(result) { return -result; }
  memcpy(output,buffer.data,buffer.size);return buffer.size; }
int c3fake_advertised(void) { return g_advertised; }
void c3fake_notify_error(int error) { g_notify_error=error; }
int c3fake_notifications(void) { return g_notifications; }
size_t c3fake_notification(void *output)
{ memcpy(output,g_notification,g_notification_size);return g_notification_size; }
void c3fake_mtu(uint16_t mtu) { g_mtu=mtu; }
"""

class EspBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder=tempfile.TemporaryDirectory(prefix='uthome-esp-bindings-')
        cls.addClassCleanup(cls.folder.cleanup)
        folder=Path(cls.folder.name)
        (folder/'sdk.h').write_text(SDK_HEADER,encoding='utf-8')
        (folder/'fake.c').write_text(FAKE_SOURCE,encoding='utf-8')
        headers=['lwip/inet.h','esp_tls.h','esp_tls_private.h','lwip/sockets.h','esp_event.h','esp_netif.h','esp_wifi.h',
                 'esp_random.h','nimble/nimble_port.h','nimble/nimble_port_freertos.h',
                 'host/ble_hs.h','host/util/util.h','services/gap/ble_svc_gap.h',
                 'services/gatt/ble_svc_gatt.h']
        for name in headers:
            path=folder/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "sdk.h"\n',encoding='utf-8')
        sources=['firmware/hal/ports/esp32c3/'+name+'.c' for name in ['c3wsta','c3gsvc','c3mqtt']]
        sources+=['firmware/src/network/'+name+'.c' for name in ['netble','netretry','netbroker','netmqtt']]
        sources+=['firmware/src/runtime/sampcache.c']
        includes=[folder,ROOT/'firmware/src/config',ROOT/'firmware/include',ROOT/'firmware/src/network',
                  ROOT/'firmware/src/services',ROOT/'firmware/src/runtime',
                  ROOT/'firmware/hal/ports/esp32c3',ROOT/'firmware/hal/ports/esp32c3/network']
        cls.binary=folder/'bindings.dll'
        command=[compiler,'-std=c99','-Wall','-Wextra','-Werror','-pedantic','-shared',*(['-fPIC'] if sys.platform!='win32' else []),
                 '-DUTHOME_NETWORK_WIFI=1','-DUTHOME_NETWORK_BLE=1',
                 '-DUTHOME_MQTT_VALUE_BYTES=112','-DUTHOME_MQTT_PACKET_BYTES=384',
                 *['-I'+str(path) for path in includes],
                 *[str(ROOT/path) for path in sources],str(folder/'fake.c'),
                 '-o',str(cls.binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:
            raise AssertionError(built.stdout+built.stderr)
        # Compile owner composition against small explicit boundary declarations.
        # These doubles do not replace any production header or native library.
        (folder/'esp_mac.h').write_text('#include "sdk.h"\n')
        (folder/'activate.h').write_text(
            '#include <stdint.h>\ntypedef struct { uint32_t generation; int state; } '
            'activate_status_t;\nint activate_get_status(activate_status_t *p_status);\n')
        (folder/'shared.h').write_text(
            '#include "sampcache.h"\nint shared_source(uint16_t source,'
            'sampcache_entry_t *entry,uint64_t *now);\n')
        (folder/'netsetup.h').write_text(
            '#include <stdint.h>\n#define UTHOME_SETUP_MQTT 0U\n'
            '#define UTHOME_SETUP_TX_POWER 0U\n'
            'static const char g_setup_ssid[33]="ap";\n'
            'static const char g_setup_password[64]="password";\n')
        for wifi,ble in [(1,0),(0,1),(1,1)]:
            compiled=subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror',
                '-pedantic','-c',f'-DUTHOME_NETWORK_WIFI={wifi}',f'-DUTHOME_NETWORK_BLE={ble}',
                *['-I'+str(path) for path in includes],'-I'+str(ROOT/'firmware/app'),
                str(ROOT/'firmware/app/c3net.c'),'-o',str(folder/f'owner-{wifi}-{ble}.o')],
                capture_output=True,text=True)
            if compiled.returncode:
                raise AssertionError(compiled.stdout+compiled.stderr)
        cls.next_instance=0

    def setUp(self):
        type(self).next_instance+=1
        path=Path(self.folder.name)/f'instance-{self.next_instance}.dll'
        shutil.copyfile(self.binary,path)
        self.lib=c.CDLL(str(path))
        self.addCleanup(self.unload)
        self.lib.c3fake_input.argtypes=[c.c_int,c.c_char_p,c.c_size_t]
        self.lib.c3fake_output.argtypes=[c.c_int,c.c_void_p]
        self.lib.c3fake_output.restype=c.c_size_t
        self.lib.c3wsta_start.argtypes=[c.c_char_p,c.c_char_p,c.c_int32]
        self.lib.c3wsta_step.argtypes=[c.c_uint64]
        self.lib.c3wsta_online.restype=c.c_bool
        self.lib.c3fake_ble_step.argtypes=[c.c_uint64]
        self.lib.c3fake_notification.argtypes=[c.c_void_p]
        self.lib.c3fake_notification.restype=c.c_size_t
        self.lib.c3fake_read.argtypes=[c.c_int,c.c_int,c.c_void_p]

    def unload(self):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(self.lib._handle)

    def output(self,fd=0):
        output=(c.c_uint8*4096)()
        size=self.lib.c3fake_output(fd,output)
        return bytes(output[:size])


    def mqtt_session(self,absent=range(1,8)):
        """Real c3mqtt over the TLS double; whole-packet writes; slot 0 present."""
        self.lib.c3fake_mqtt_step.argtypes=[c.c_uint64,c.c_int]
        self.lib.c3fake_take_output.argtypes=[c.c_int,c.c_void_p]
        self.lib.c3fake_take_output.restype=c.c_size_t
        self.lib.c3fake_generation.argtypes=[c.c_uint32]
        self.lib.c3fake_capture_result.argtypes=[c.c_int,c.c_int]
        self.lib.c3mqtt_status.argtypes=[c.c_void_p]
        for slot in absent:
            self.lib.c3fake_capture_result(slot,1)
        self.assertEqual(self.lib.c3fake_mqtt_start(),0)
        self.lib.c3fake_handshake(1)
        self.lib.c3fake_write_limit(4096)
        self.mqtt_now=0
        self.mqtt_stream=bytearray()
        self.publishes=[]  # (owner time, retain bit, topic, payload)
        self.mqtt_connects=0

    def mqtt_pump(self,steps,dt=1,online=1):
        buffer=(c.c_uint8*4096)()
        for _ in range(steps):
            self.mqtt_now+=dt
            # Intended session resets return -1; outcomes are checked on the wire.
            self.lib.c3fake_mqtt_step(self.mqtt_now,online)
            size=self.lib.c3fake_take_output(0,buffer)
            self.mqtt_stream.extend(bytes(buffer[:size]))
            self.mqtt_parse()

    def mqtt_parse(self):
        data=self.mqtt_stream
        while len(data)>=2:
            length=0
            shift=0
            index=1
            while True:
                if index>=len(data):
                    return
                byte=data[index]
                length|=(byte&127)<<shift
                shift+=7
                index+=1
                if not byte&128:
                    break
            if len(data)<index+length:
                return
            packet=bytes(data[:index+length])
            del data[:index+length]
            body=packet[index:]
            kind=packet[0]>>4
            if kind==1:
                self.mqtt_connects+=1
                self.lib.c3fake_input(0,b'\x20\x02\x00\x00',4)
            elif kind==3:
                size=int.from_bytes(body[:2],'big')
                self.publishes.append((self.mqtt_now,packet[0]&1,
                                       body[2:2+size].decode('ascii'),body[2+size:]))
            elif kind==8:
                self.lib.c3fake_input(0,b'\x90\x03'+body[:2]+b'\x00',5)
            elif kind==12:
                self.lib.c3fake_input(0,b'\xd0\x00',2)
            else:
                self.fail(f'unexpected packet {packet!r}')


    CAPTURE_RETRIES=100  # Mirrors C3MQTT_CAPTURE_RETRIES in c3mqtt.c.


    def test_mqtt_target_buffers_publish_binary_telemetry_without_discovery(self):
        from tools.target.network_client import decode_mqtt
        self.mqtt_session()
        self.mqtt_pump(400)
        self.assertEqual(self.publishes[0][2:],
                         ('uthome/v1/node/availability', b'online'))
        telemetry = [p for p in self.publishes if '/telemetry/' in p[2]]
        self.assertTrue(telemetry)
        self.assertFalse(any(p[2].startswith('homeassistant/') for p in self.publishes))
        for _, retain, topic, payload in telemetry:
            self.assertEqual(retain, 0)
            self.assertEqual(len(payload), 112)
            self.assertEqual(decode_mqtt(topic, payload)['source_index'], 0)

    def test_mqtt_tls_rearms_socket_after_connect_timeouts_and_reopen(self):
        self.mqtt_session(absent=range(8))
        self.assertEqual(self.lib.c3mqtt_transactions(), 0)
        self.lib.c3fake_connect_timeouts.argtypes = [c.c_uint32]
        self.lib.c3fake_tls_polls.restype = c.c_uint32
        self.lib.c3fake_connect_timeouts(3)
        self.mqtt_pump(3)
        self.assertEqual(self.lib.c3fake_tls_polls(), 3)
        self.assertEqual(self.mqtt_connects, 0)
        self.mqtt_pump(20)
        self.assertEqual(self.lib.c3fake_tls_polls(), 4)
        self.assertEqual(self.mqtt_connects, 1)
        self.assertTrue(self.lib.c3mqtt_ready())

        self.lib.c3fake_connect_timeouts(2)
        self.lib.c3fake_generation(8)
        self.mqtt_pump(1,online=0)
        self.mqtt_pump(40, dt=1000)
        self.assertEqual(self.lib.c3fake_tls_polls(), 7)
        self.assertEqual(self.mqtt_connects, 2)
        self.assertTrue(self.lib.c3mqtt_ready())

    def test_mqtt_command_subscription_and_priority_result(self):
        self.assertEqual(self.lib.c3fake_mqtt_start(),0)
        self.assertEqual(self.lib.c3mqtt_transactions(),0)
        self.lib.c3fake_handshake(1)
        self.lib.c3fake_mqtt_step.argtypes=[c.c_uint64,c.c_int]
        self.lib.c3mqtt_request.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
        self.lib.c3mqtt_result.argtypes=[c.c_char_p,c.c_size_t]
        self.lib.c3mqtt_status.argtypes=[c.c_void_p]
        view=(c.c_uint32*4)();connack=suback=False
        for now in range(1,250):
            self.lib.c3fake_mqtt_step(now,1)
            output=self.output(0)
            if not connack and output.startswith(b'\x10'):
                self.lib.c3fake_input(0,b'\x20\x02\x00\x00',4);connack=True
            if connack and not suback and b'transaction/request' in output:
                self.lib.c3fake_input(0,b'\x90\x03\x00\x01\x00',5);suback=True
            self.lib.c3mqtt_status(view)
            if suback and view[0]==5:break
        self.assertTrue(suback)
        topic=b'uthome/v1/test-node/transaction/request'
        # c3fake_mqtt_start uses the node identity below, not the HTTP fixture.
        output=self.output(0)
        start=output.index(b'uthome/v1/')
        # Read the explicit subscription from the staged wire packet.
        end=output.index(b'/transaction/request')+len(b'/transaction/request')
        start=output.rfind(b'uthome/v1/',0,end)
        topic=output[start:end]
        payload=bytes(range(96))
        body=len(topic).to_bytes(2,'big')+topic+payload
        packet=b'\x30'+bytes([(len(body)%128)|128,len(body)//128])+body
        self.lib.c3fake_input(0,packet,len(packet))
        for tick in range(now+1,now+150):self.lib.c3fake_mqtt_step(tick,1)
        received=c.create_string_buffer(96);size=c.c_size_t()
        self.assertEqual(self.lib.c3mqtt_request(received,96,c.byref(size)),0)
        self.assertEqual(received.raw,payload)
        self.assertEqual(size.value,96)
        self.assertEqual(self.lib.c3mqtt_result(payload,96),0)
        self.assertEqual(self.lib.c3mqtt_result(payload,96),1)
        for tick in range(now+150,now+350):self.lib.c3fake_mqtt_step(tick,1)
        output=self.output(0)
        self.assertIn(b'transaction/result'+payload,output)
        self.assertEqual(self.lib.c3mqtt_result(payload,97),-1)

    def test_mqtt_idle_generation_preserves_session_and_queries_after_keepalive(self):
        from tools.transaction.client import query
        self.mqtt_session()
        self.assertEqual(self.lib.c3mqtt_transactions(),0)
        self.lib.c3mqtt_request.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
        self.lib.c3mqtt_result.argtypes=[c.c_char_p,c.c_size_t]
        self.mqtt_pump(100)
        self.assertTrue(any('/telemetry/' in item[2] for item in self.publishes))
        self.lib.c3fake_generation(5)
        for slot in range(8):
            self.lib.c3fake_capture_result(slot,1)
        self.mqtt_pump(1)
        self.assertTrue(self.lib.c3mqtt_ready())
        self.assertEqual(self.mqtt_connects,1)
        self.publishes.clear()
        # Exercise the real adapter/parser with no samples across two keepalives.
        # The owner is doubled here: its 96-byte reply is opaque to MQTT.
        self.mqtt_pump(32000)
        topic=b'uthome/v1/node/transaction/request'
        for nonce in range(1,6):
            payload=query(nonce)
            body=len(topic).to_bytes(2,'big')+topic+payload
            packet=b'\x30'+bytes([(len(body)%128)|128,len(body)//128])+body
            self.lib.c3fake_input(0,packet,len(packet))
            self.mqtt_pump(100)
            received=c.create_string_buffer(96)
            size=c.c_size_t()
            self.assertEqual(self.lib.c3mqtt_request(received,96,c.byref(size)),0)
            self.assertEqual((size.value,received.raw),(96,payload))
            self.assertEqual(self.lib.c3mqtt_result(payload,96),0)
            self.mqtt_pump(1000)
        self.assertEqual([(topic,data) for _,_,topic,data in self.publishes],
                         [('uthome/v1/node/transaction/result',query(nonce))
                          for nonce in range(1,6)])
        view=(c.c_uint32*4)()
        self.assertEqual(self.lib.c3mqtt_status(view),0)
        self.assertEqual((view[0],view[1],self.mqtt_connects),(5,0,1))


    def test_wifi_association_ip_loss_and_reconnect(self):
        self.assertEqual(self.lib.c3wsta_start(b'my-ap',b'password',0),0)
        self.assertEqual(self.lib.c3wsta_step(0),0)
        self.assertEqual(self.lib.c3fake_connects(),0)
        self.lib.c3fake_event(1)
        self.lib.c3wsta_step(1)
        self.assertEqual(self.lib.c3fake_connects(),1)
        self.assertFalse(self.lib.c3wsta_online())
        self.lib.c3fake_event(4)
        self.lib.c3wsta_step(2)
        self.assertTrue(self.lib.c3wsta_online())
        self.lib.c3fake_event(2)
        self.lib.c3wsta_step(3)
        self.assertFalse(self.lib.c3wsta_online())
        self.lib.c3wsta_step(1002)
        self.assertEqual(self.lib.c3fake_connects(),1)
        self.lib.c3wsta_step(1003)
        self.assertEqual(self.lib.c3fake_connects(),2)
        self.assertEqual(self.lib.c3fake_power_calls(),0)

    def test_wifi_tx_cap_precedes_connect_and_reapplies_on_reconnect(self):
        self.assertEqual(self.lib.c3wsta_start(b'my-ap',b'password',34),0)
        self.assertEqual(self.lib.c3fake_power_calls(),0)
        self.lib.c3wsta_step(0)
        self.assertEqual(self.lib.c3fake_power_calls(),0)
        self.lib.c3fake_event(1)
        self.lib.c3wsta_step(1)
        self.assertEqual(self.lib.c3fake_power_calls(),1)
        self.assertEqual([self.lib.c3fake_wifi_operation(i) for i in range(3)],
                         [11,1034,13])
        self.lib.c3fake_event(4)
        self.lib.c3wsta_step(2)
        self.assertTrue(self.lib.c3wsta_online())
        self.lib.c3fake_event(2)
        self.lib.c3wsta_step(3)
        self.lib.c3wsta_step(1002)
        self.assertEqual(self.lib.c3fake_power_calls(),1)
        self.lib.c3wsta_step(1003)
        self.assertEqual([self.lib.c3fake_wifi_operation(i) for i in range(5)],
                         [11,1034,13,1034,13])
        self.assertEqual(self.lib.c3fake_power_calls(),2)
        self.assertEqual(self.lib.c3fake_connects(),2)

    def test_wifi_tx_cap_failure_reports_stage_and_retries_without_connect(self):
        self.lib.c3wsta_start(b'my-ap',b'password',34)
        self.lib.c3fake_event(1)
        self.lib.c3fake_power_error(0x4325)
        self.assertEqual(self.lib.c3wsta_step(0),0)
        failed=self.wifi_status()
        self.assertEqual((failed.stage,failed.sdk_error,failed.retry_state,
                          failed.deadline_ms),(15,0x4325,0,1000))
        self.assertFalse(failed.flags&32)
        self.assertEqual(self.lib.c3fake_connects(),0)
        self.lib.c3wsta_step(999)
        self.assertEqual(self.lib.c3fake_power_calls(),1)
        self.lib.c3fake_power_error(0)
        self.assertEqual(self.lib.c3wsta_step(1000),0)
        self.assertEqual(self.lib.c3fake_power_calls(),2)
        self.assertEqual(self.lib.c3fake_connects(),1)
        self.assertEqual((self.wifi_status().stage,self.wifi_status().sdk_error),(13,0))

    def test_wifi_tx_cap_argument_bounds_preserve_startup_and_signed_sdk_abi(self):
        for power in (-2**31,-1,1,7,85,128,2**31-1,8,84):
            with self.subTest(power=power):
                path=Path(self.folder.name)/f'wifi-cap-{power}.dll'
                shutil.copyfile(self.binary,path)
                library=c.CDLL(str(path))
                try:
                    library.c3wsta_start.argtypes=[c.c_char_p,c.c_char_p,c.c_int32]
                    invalid=power not in (8,84)
                    self.assertEqual(library.c3wsta_start(b'my-ap',b'password',power),
                                     -1 if invalid else 0)
                    if invalid:
                        self.assertEqual(library.c3fake_wifi_setup_calls(),0)
                        self.assertEqual(self.wifi_status(library).flags,0)
                        self.assertEqual(library.c3wsta_start(b'my-ap',b'password',0),0)
                    library.c3fake_event(1)
                    library.c3wsta_step(0)
                    self.assertEqual(library.c3fake_power_calls(),0 if invalid else 1)
                    if not invalid:
                        self.assertEqual(library.c3fake_wifi_operation(1),1000+power)
                finally:
                    if sys.platform=='win32':
                        import _ctypes
                        _ctypes.FreeLibrary(library._handle)

    def wifi_status(self,library=None):
        library=library or self.lib
        library.c3wsta_status.argtypes=[c.POINTER(WifiStatus)]
        status=WifiStatus()
        self.assertEqual(library.c3wsta_status(c.byref(status)),0)
        return status

    def test_wifi_status_initial_and_successful_start_reads_do_no_sdk_work(self):
        before=self.wifi_status()
        self.assertEqual((before.stage,before.flags,before.sdk_error),(0,0,0))
        self.assertEqual(self.lib.c3wsta_status(None),-1)
        self.assertEqual(self.lib.c3wsta_start(b'my-ap',b'password',0),0)
        state=self.wifi_status()
        self.assertEqual((state.stage,state.flags,state.sdk_error),(12,3,0))
        calls=self.lib.c3fake_wifi_setup_calls()
        for _ in range(10):
            self.assertEqual(bytes(self.wifi_status()),bytes(state))
        self.assertEqual(self.lib.c3fake_wifi_setup_calls(),calls)
        self.assertEqual(self.lib.c3fake_connects(),0)

    def test_wifi_status_retains_every_failed_startup_stage(self):
        for stage in range(1,12):
            with self.subTest(stage=stage):
                path=Path(self.folder.name)/f'wifi-failure-{stage}.dll'
                shutil.copyfile(self.binary,path)
                library=c.CDLL(str(path))
                try:
                    library.c3fake_wifi_failure(stage)
                    library.c3wsta_start.argtypes=[c.c_char_p,c.c_char_p,c.c_int32]
                    self.assertEqual(library.c3wsta_start(b'my-ap',b'password',0),-1)
                    state=self.wifi_status(library)
                    self.assertEqual(state.stage,stage)
                    self.assertEqual(state.sdk_error,257 if stage==3 else 0x4321)
                    self.assertTrue(state.flags&1)
                    self.assertFalse(state.flags&2)
                    self.assertTrue(state.flags&32)
                    self.assertEqual(library.c3fake_wifi_setup_calls(),stage)
                    self.assertEqual(bytes(self.wifi_status(library)),bytes(state))
                finally:
                    if sys.platform=='win32':
                        import _ctypes
                        _ctypes.FreeLibrary(library._handle)

    def test_wifi_status_copies_ip_reason_and_clears_stale_addresses(self):
        self.lib.c3wsta_start(b'my-ap',b'password',0)
        self.lib.c3fake_event(1)
        self.lib.c3wsta_step(0)
        attempt=self.wifi_status()
        self.assertEqual((attempt.stage,attempt.retry_state,attempt.deadline_ms),(13,1,30000))
        for event in (5,3,2):
            self.lib.c3fake_wifi_ip(192,168,4,31)
            observed=self.wifi_status()
            self.assertEqual(bytes(observed.address),bytes([192,168,4,31]))
            self.assertTrue(observed.flags&8)
            if event==2:
                self.lib.c3fake_wifi_disconnected(201)
            else:
                self.lib.c3fake_event(event)
            cleared=self.wifi_status()
            self.assertEqual(bytes(cleared.address),bytes(4))
            self.assertFalse(cleared.flags&8)
        self.assertEqual(cleared.disconnect_reason,201)

    def test_wifi_status_retains_connect_and_disconnect_sdk_errors(self):
        self.lib.c3wsta_start(b'my-ap',b'password',0)
        self.lib.c3fake_event(1)
        self.lib.c3fake_wifi_errors(0x4322,0)
        self.assertEqual(self.lib.c3wsta_step(0),0)
        failed=self.wifi_status()
        self.assertEqual((failed.stage,failed.sdk_error,failed.retry_state),(13,0x4322,0))
        self.lib.c3fake_wifi_errors(0,0x4323)
        self.lib.c3wsta_step(1000)
        self.assertEqual(self.lib.c3wsta_step(31000),-1)
        cancelled=self.wifi_status()
        self.assertEqual((cancelled.stage,cancelled.sdk_error),(14,0x4323))
        self.assertTrue(cancelled.flags&32)

    def test_wifi_timed_out_attempt_waits_for_cancellation(self):
        self.lib.c3wsta_start(b'my-ap',b'password',0)
        self.lib.c3fake_event(1)
        self.lib.c3wsta_step(0)
        self.lib.c3wsta_step(30000)
        self.assertEqual(self.lib.c3fake_disconnects(),1)
        self.lib.c3wsta_step(60000)
        self.assertEqual(self.lib.c3fake_connects(),1)
        self.lib.c3fake_event(2)
        self.lib.c3wsta_step(60001)
        self.assertEqual(self.lib.c3fake_connects(),2)

    def test_ble_registers_real_uuid_reads_and_denies_writes(self):
        self.assertEqual(self.lib.c3fake_ble_start(),0)
        self.assertEqual(self.lib.c3fake_advertised(),1)
        output=(c.c_uint8*128)()
        self.assertEqual(self.lib.c3fake_read(0,0,output),44)
        self.assertEqual(bytes(output[12:21]),b'test-node')
        self.assertEqual(self.lib.c3fake_read(1,0,output),20)
        self.assertEqual(self.lib.c3fake_read(0,1,output),-3)

    def test_ble_subscription_busy_retry_disconnect_and_mtu(self):
        self.lib.c3fake_ble_start()
        self.lib.c3fake_ble_step(0)
        self.assertEqual(self.lib.c3fake_notifications(),0)
        self.lib.c3fake_gap(0,0)
        self.lib.c3fake_gap(2,1)
        self.lib.c3fake_notify_error(-1)
        self.assertEqual(self.lib.c3fake_ble_step(1),-1)
        self.lib.c3fake_notify_error(0)
        self.lib.c3fake_ble_step(2)
        output=(c.c_uint8*128)()
        size=self.lib.c3fake_notification(output)
        self.assertEqual((size,output[4],output[5]),(20,0,10))
        self.lib.c3fake_ble_step(3)
        self.lib.c3fake_notification(output)
        self.assertEqual(output[4],1)
        self.lib.c3fake_mtu(128)
        self.lib.c3fake_gap(3,0)
        self.lib.c3fake_ble_step(4)
        size=self.lib.c3fake_notification(output)
        self.assertEqual((size,output[4],output[5]),(120,0,1))
        count=self.lib.c3fake_notifications()
        self.lib.c3fake_gap(1,0)
        self.lib.c3fake_ble_step(5)
        self.assertEqual(self.lib.c3fake_advertised(),2)
        self.assertEqual(self.lib.c3fake_notifications(),count)


    def test_mqtt_diagnostics_do_not_start_tls_or_reset_failure_counts(self):
        self.lib.c3mqtt_status.argtypes=[c.c_void_p]
        view=(c.c_uint32*4)()
        self.assertEqual(self.lib.c3mqtt_status(view),0)
        self.assertEqual(list(view),[6,0,0,0])
        self.assertEqual(self.lib.c3fake_deleted(),0)
        self.assertEqual(self.lib.c3fake_mqtt_start(),0)
        self.lib.c3fake_mqtt_step.argtypes=[c.c_uint64,c.c_int]
        self.lib.c3fake_handshake(0)
        self.lib.c3fake_mqtt_step(1,1)
        self.lib.c3mqtt_status(view)
        self.assertEqual(view[0],1)
        self.lib.c3fake_mqtt_step(2,0)
        self.lib.c3mqtt_status(view)
        self.assertEqual(list(view),[0,1,0,0])
        self.lib.c3mqtt_status(view)
        self.assertEqual(list(view),[0,1,0,0])
        self.assertEqual(self.lib.c3fake_deleted(),1)

    def test_mqtt_tls_partial_start_and_upstream_loss_release_once(self):
        self.lib.c3fake_mqtt_step.argtypes=[c.c_uint64,c.c_int]
        self.assertEqual(self.lib.c3fake_mqtt_start(),0)
        self.assertEqual(self.lib.c3fake_mqtt_start(),-1)
        self.lib.c3fake_handshake(0)
        self.assertEqual(self.lib.c3fake_mqtt_step(1,1),0)
        self.assertEqual(self.lib.c3fake_deleted(),0)
        self.lib.c3fake_mqtt_step(2,0)
        self.assertEqual(self.lib.c3fake_deleted(),1)
        self.lib.c3fake_mqtt_step(3,0)
        self.assertEqual(self.lib.c3fake_deleted(),1)


class SetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('network_setup',ROOT/'tools/target/network_setup.py')
        cls.setup=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.setup)

    def configuration(self,folder):
        values=dict(ssid='my-ap',password='password')
        path=folder/'private.json'
        path.write_text(json.dumps(values))
        return path,values

    def test_ssid_utf8_header_preserves_exact_bytes_for_both_char_modes(self):
        compiler=shutil.which('gcc')
        self.assertIsNotNone(compiler,'GCC required for generated-header checks')
        names=['Wai Yan\u2019s iPhone','\u00e9'*16,'\U0001f34e'*8,
               'e\u0301'*10+'ab','quote"\\network']
        with tempfile.TemporaryDirectory(prefix='uthome-ssid-utf8-') as temporary:
            folder=Path(temporary)
            source,config=self.configuration(folder)
            for name in names:
                with self.subTest(ssid=name):
                    config['ssid']=name
                    source.write_text(json.dumps(config))
                    header,_=self.setup.generate(source)
                    (folder/'netsetup.h').write_bytes(header)
                    expected=name.encode('utf-8').ljust(33,b'\0')
                    fixture='''#include "netsetup.h"
#include <string.h>
int main(void)
{
    const unsigned char expected[33] = {EXPECTED};
    if ((33U != sizeof(g_setup_ssid)) ||
        (0 != memcmp(g_setup_ssid, expected, sizeof(expected))))
    {
        return 1;
    }
    if ('p' != g_setup_password[0])
    {
        return 2;
    }
    return 0;
}
'''.replace('EXPECTED',','.join(f'0x{byte:02x}U' for byte in expected))
                    (folder/'main.c').write_text(fixture)
                    for mode in ('-fsigned-char','-funsigned-char'):
                        binary=folder/('ssid.exe' if sys.platform=='win32' else 'ssid')
                        built=subprocess.run([compiler,'-std=c99','-Wall','-Wextra',
                            '-Werror','-pedantic',mode,str(folder/'main.c'),
                            '-o',str(binary)],capture_output=True,text=True)
                        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                        observed=subprocess.run([str(binary)],capture_output=True,text=True)
                        self.assertEqual(observed.returncode,0,observed.stdout+observed.stderr)

    def test_ssid_utf8_bounds_and_nonprintable_are_rejected(self):
        invalid=['','x'*33,'\u00e9'*16+'a','\U0001f34e'*9,
                 '\ud800','\udfff','AP\0name','AP\nname','AP\x1fname',
                 'AP\x7fname','AP\x85name','AP\u2028name','AP\u200bname',None,True]
        with tempfile.TemporaryDirectory() as temporary:
            source,config=self.configuration(Path(temporary))
            for name in invalid:
                with self.subTest(ssid=repr(name)):
                    config['ssid']=name
                    source.write_text(json.dumps(config))
                    with self.assertRaises(ValueError):
                        self.setup.generate(source)

    def test_utf8_ssid_does_not_relax_other_ascii_credentials(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            source,config=self.configuration(folder)
            config['ssid']='Wai Yan\u2019s iPhone'
            self.setup.generate(source)
            source.write_text(json.dumps(config))
            self.setup.generate(source)
            for field,value in [('password','p\u00e9ssword')]:
                with self.subTest(field=field):
                    source.write_text(json.dumps({**config,field:value}))
                    with self.assertRaises(ValueError):
                        self.setup.generate(source)
            ca=folder/'broker-ca.pem'
            ca.write_bytes(b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
            mqtt=dict(address='127.0.0.1',server_name='broker.test',port=8883,
                      user='device',password='password',ca_file=ca.name)
            config['mqtt']=mqtt
            source.write_text(json.dumps(config))
            self.setup.generate(source)
            for field in ('server_name','user','password'):
                with self.subTest(mqtt_field=field):
                    config['mqtt']={**mqtt,field:'caf\u00e9'}
                    source.write_text(json.dumps(config))
                    with self.assertRaises(ValueError):
                        self.setup.generate(source)


    def test_private_setup_staging_and_mutation_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            source,values=self.configuration(folder)
            (folder/'app').mkdir()
            (folder/'source-manifest.json').write_text('{"sources":{}}')
            self.setup.stage(folder,source)
            manifest=json.loads((folder/'source-manifest.json').read_text())
            self.setup.verify(folder,manifest['network_setup'])
            values['password']='different-password'
            source.write_text(json.dumps(values))
            with self.assertRaises(ValueError):
                self.setup.verify(folder,manifest['network_setup'])

    def test_service_build_modes_reject_unsafe_mixes_before_side_effects(self):
        from tools.target import qualify_kernel as qualification
        cases=[dict(network_services='ble'),
               dict(network_services='wifi',runtime_port=True,configuration_app=True,static_net_queues=True),
               dict(network_services='ble',runtime_port=True,configuration_app=True,static_net_queues=True,ble_probe=True),
               dict(network_settings=Path('private.json'))]
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'must-not-exist'
            for arguments in cases:
                arguments=dict(arguments)
                settings=arguments.pop('network_settings',None)
                with self.assertRaises(qualification.QualificationError):
                    qualification.build(Path('kernel'),Path('sdk'),output,
                        selection=qualification.resolve_mode(**arguments), network_settings=settings)
                self.assertFalse(output.exists())

    def test_mqtt_private_inputs_validate_and_track_ca_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            source,config=self.configuration(folder)
            ca=folder/'broker-ca.pem'
            ca.write_bytes(b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
            mqtt=dict(address='127.0.0.1',server_name='broker.test',port=8883,
                      user='device',password='password',ca_file=ca.name)
            config['mqtt']=mqtt
            source.write_text(json.dumps(config))
            header,record=self.setup.generate(source)
            self.assertRegex(header,rb'UTHOME_SETUP_MQTT\s+\(1U\)')
            self.assertIn('mqtt_ca_sha256',record)
            for field,value in [('address','broker.test'),('server_name',''),
                                ('port',True),('user','x'*65),('password',''),
                                ('server_name','broker/name')]:
                config['mqtt']={**mqtt,field:value}
                source.write_text(json.dumps(config))
                with self.assertRaises(ValueError):
                    self.setup.generate(source)
            config['mqtt']=mqtt
            source.write_text(json.dumps(config))
            ca.write_bytes(b'not a CA')
            with self.assertRaises(ValueError):
                self.setup.generate(source)

    def test_private_setup_rejects_unknown_keys_duplicates_and_bad_bounds(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            source,original=self.configuration(folder)
            for key,value in [('ssid','x'*33),('password','short'),('token','short'),
                              ('port',True),('port',0),('unknown','value')]:
                changed=dict(original)
                changed[key]=value
                source.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    self.setup.generate(source)
            source.write_text('{"ssid":"one","ssid":"two"}')
            with self.assertRaises(ValueError):
                self.setup.generate(source)

if __name__=='__main__':
    unittest.main()
