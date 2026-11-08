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
int c3fake_recv(int fd,void *data,size_t size,int flags);
int c3fake_send(int fd,const void *data,size_t size,int flags);
int c3fake_socket(int domain,int type,int protocol);
int c3fake_bind(int fd,const struct sockaddr *address,size_t size);
int c3fake_listen(int fd,int backlog);
int c3fake_setsockopt(int fd,int level,int name,const void *value,size_t size);
int c3fake_accept(int fd,struct sockaddr *address,void *size);
int c3fake_fcntl(int fd,int command,...);
int c3fake_close(int fd);
uint16_t c3fake_htons(uint16_t value);
uint32_t c3fake_htonl(uint32_t value);
typedef struct { int socket; } esp_tls_t;
typedef struct { const uint8_t *servercert_buf; unsigned int servercert_bytes;
 const uint8_t *serverkey_buf; unsigned int serverkey_bytes;
 unsigned int tls_handshake_timeout_ms; } esp_tls_cfg_server_t;
typedef struct { const unsigned char *cacert_buf; unsigned int cacert_bytes;
 const char *common_name; bool non_block; int timeout_ms; } esp_tls_cfg_t;
int esp_tls_conn_new_async(const char *,int,int,const esp_tls_cfg_t *,esp_tls_t *);
int esp_tls_conn_destroy(esp_tls_t *);
int inet_pton(int,const char *,void *);
esp_tls_t *esp_tls_init(void);
int esp_tls_server_session_init(esp_tls_cfg_server_t *cfg,int fd,esp_tls_t *tls);
int esp_tls_server_session_continue_async(esp_tls_t *tls);
void esp_tls_server_session_delete(esp_tls_t *tls);
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
#include "c3https.h"
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
static cfgmux_t *gp_test_mux;
static int g_accept=-1;
static int g_listener=50;
static int g_deleted;
static int g_closed[64];
static esp_tls_t g_tls[2];
static bool g_allocated[2];
static int g_handshake;
static int g_fail_setup;
static int g_write_limit=17;
static char g_input[64][1100];
static size_t g_input_size[64];
static uint8_t g_output[64][4096];
static size_t g_output_size[64];
static int g_connects;
static int g_disconnects;
static int g_connect_error;
static int g_disconnect_error;
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
int c3fake_socket(int domain,int type,int protocol)
{ (void)domain;(void)type;(void)protocol;return g_listener; }
int c3fake_bind(int fd,const struct sockaddr *address,size_t size)
{ (void)fd;(void)address;(void)size;return 0; }
int c3fake_setsockopt(int fd,int level,int name,const void *value,size_t size)
{ (void)fd;(void)level;(void)name;(void)value;(void)size;return 0; }
int c3fake_listen(int fd,int backlog) { (void)fd;return backlog==2?0:-1; }
int c3fake_accept(int fd,struct sockaddr *address,void *size)
{ int value=g_accept;(void)fd;(void)address;(void)size;g_accept=-1;return value; }
int c3fake_fcntl(int fd,int command,...) { (void)fd;(void)command;return 0; }
int c3fake_close(int fd) { if(fd>=0 && fd<64) { ++g_closed[fd]; } return 0; }
uint16_t c3fake_htons(uint16_t value) { return value; }
uint32_t c3fake_htonl(uint32_t value) { return value; }
esp_tls_t *esp_tls_init(void)
{ for(int i=0;i<2;++i) { if(!g_allocated[i]) { g_allocated[i]=true;return &g_tls[i]; } } return NULL; }
int inet_pton(int family,const char *text,void *out)
{ (void)family;(void)out;return strcmp(text,"127.0.0.1")==0; }
int esp_tls_conn_new_async(const char *host,int size,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls)
{ (void)host;(void)size;(void)port;(void)tls;
 return cfg->non_block && cfg->cacert_buf && cfg->common_name ? g_handshake : -1; }
int esp_tls_conn_destroy(esp_tls_t *tls)
{ esp_tls_server_session_delete(tls);return 0; }
int esp_tls_server_session_init(esp_tls_cfg_server_t *cfg,int fd,esp_tls_t *tls)
{ (void)cfg;tls->socket=fd;return g_fail_setup; }
int esp_tls_server_session_continue_async(esp_tls_t *tls)
{ (void)tls;return g_handshake; }
void esp_tls_server_session_delete(esp_tls_t *tls)
{ for(int i=0;i<2;++i) { if(tls==&g_tls[i]) { g_allocated[i]=false; } } ++g_deleted; }
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
#if UTHOME_OCCUPANCY_LAB
#include <errno.h>
int c3fake_recv(int fd,void *data,size_t size,int flags)
{ (void)flags; esp_tls_t channel={fd};int n=(int)esp_tls_conn_read(&channel,data,size);
  if(n<0){errno=EAGAIN;return -1;} return n; }
int c3fake_send(int fd,const void *data,size_t size,int flags)
{ (void)flags; esp_tls_t channel={fd};return (int)esp_tls_conn_write(&channel,data,size); }
static int radar_mode;
void c3fake_radar(int mode){radar_mode=mode;}
#endif
int esp_netif_init(void) { return 0; }
void *esp_netif_create_default_wifi_sta(void) { return &g_connects; }
int esp_event_loop_create_default(void) { return 0; }
int esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
 event_handler_t *handler,void *arg,esp_event_handler_instance_t *instance)
{ (void)base;(void)id;(void)arg;g_event=handler;if(instance) { *instance=&g_event; } return 0; }
int esp_wifi_init(const wifi_init_config_t *config) { return config->nvs_enable?-1:0; }
int esp_wifi_set_storage(int storage) { return storage; }
int esp_wifi_set_mode(int mode) { return mode==WIFI_MODE_STA?0:-1; }
int esp_wifi_set_config(int interface_id,const wifi_config_t *config)
{ (void)interface_id;return config->sta.threshold.authmode==WIFI_AUTH_WPA2_PSK?0:-1; }
int esp_wifi_start(void) { return 0; }
int esp_wifi_connect(void) { ++g_connects;return g_connect_error; }
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
static int capture(void *context,uint16_t source,sampcache_entry_t *entry,uint64_t *now)
{ (void)context;memset(entry,0,sizeof(*entry));entry->source.type=1;
  entry->source.source_index=source;entry->source.stale_ms=1000;
  memcpy(entry->source.name,"environment",12);entry->b_expected=true;entry->b_seen=true;
  entry->header.status=1;entry->header.timestamp_ms=100;
  entry->value.environment.pressure_pa=100800;entry->value.environment.temperature_c=26;
#if UTHOME_OCCUPANCY_LAB
  if(source!=2)return 1;
  strcpy(entry->source.name,radar_mode==5?"other":"presence_sensor");
  entry->source.type=UTHOME_SAMPLE_TARGETS;entry->b_seen=radar_mode!=0;
  memset(&entry->value,0,sizeof(entry->value));
  entry->value.targets.targets[2].b_present=radar_mode==2;
  entry->header.status=radar_mode==4?0:1;entry->source.stale_ms=radar_mode==3?50:1000;
#endif
  *now=150;return 0; }
int c3fake_start(void)
{ static const uint8_t cert[]="certificate";static const uint8_t key[]="key";
  static const uint8_t token[]="0123456789abcdef0123456789abcdef";
  c3https_config_t config={cert,key,token,sizeof(cert),sizeof(key),sizeof(token)-1,8443,gp_test_mux};
#if UTHOME_OCCUPANCY_LAB
  strcpy(g_info.node_id,"uthome-010203040506");config.port=6053;
#endif
  return c3https_start(&config); }
void c3fake_mux(cfgmux_t *mux) { gp_test_mux=mux;g_info.b_configuration_supported=mux!=NULL; }
int c3fake_drive(uint64_t now,int online)
{ return c3https_step(now,online!=0,&g_info,capture,NULL); }
void c3fake_accept_next(int fd) { g_accept=fd; }
void c3fake_input(int fd,const char *data,size_t size)
{ if(size<=1100) { memcpy(g_input[fd],data,size);g_input_size[fd]=size; } }
size_t c3fake_output(int fd,void *output)
{ memcpy(output,g_output[fd],g_output_size[fd]);return g_output_size[fd]; }
int c3fake_closed(int fd) { return g_closed[fd]; }
int c3fake_deleted(void) { return g_deleted; }
void c3fake_handshake(int result) { g_handshake=result; }
void c3fake_setup_error(int result) { g_fail_setup=result; }
void c3fake_write_limit(int size) { g_write_limit=size; }
void c3fake_event(int identifier)
{ g_event(NULL,identifier<=3?WIFI_EVENT:IP_EVENT,identifier,NULL); }
int c3fake_connects(void) { return g_connects; }
int c3fake_disconnects(void) { return g_disconnects; }
int c3fake_mqtt_start(void)
{ static const uint8_t ca[]="certificate";
 const c3mqtt_config_t cfg={.p_address="127.0.0.1",.p_server_name="broker.test",
 .p_user="user",.p_password="password",.p_ca=ca,.ca_size=sizeof(ca),.port=8883};
 return c3mqtt_start(&cfg,"node"); }
int c3fake_mqtt_step(uint64_t now,int online)
{ return c3mqtt_step(now,online!=0,&g_info,capture,NULL); }
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
        headers=['lwip/inet.h','esp_tls.h','lwip/sockets.h','esp_event.h','esp_netif.h','esp_wifi.h',
                 'esp_random.h','nimble/nimble_port.h','nimble/nimble_port_freertos.h',
                 'host/ble_hs.h','host/util/util.h','services/gap/ble_svc_gap.h',
                 'services/gatt/ble_svc_gatt.h']
        for name in headers:
            path=folder/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "sdk.h"\n',encoding='utf-8')
        sources=['firmware/hal/ports/esp32c3/'+name+'.c' for name in ['c3https','c3wsta','c3gsvc','c3mqtt']]
        sources+=['firmware/src/network/'+name+'.c' for name in ['nethttp','netread','netble','netretry','netbroker','netmqtt']]
        sources+=['firmware/src/runtime/sampcache.c']
        sources+=['firmware/src/config/'+name+'.c' for name in ['cfgmux','cfgctrl','cfgstore','cfgwire','cfgbytes']]
        includes=[folder,ROOT/'firmware/src/config',ROOT/'firmware/include',ROOT/'firmware/src/network',
                  ROOT/'firmware/src/services',ROOT/'firmware/src/runtime',
                  ROOT/'firmware/hal/ports/esp32c3',ROOT/'firmware/hal/ports/esp32c3/network']
        aliases={name:'c3fake_'+name for name in
                 ['socket','bind','listen','accept','fcntl','close','htons','htonl','setsockopt']}
        cls.binary=folder/'bindings.dll'
        command=[compiler,'-std=c99','-Wall','-Wextra','-Werror','-pedantic','-shared',*(['-fPIC'] if sys.platform!='win32' else []),
                 '-DUTHOME_NETWORK_WIFI=1','-DUTHOME_NETWORK_BLE=1',
                 *['-I'+str(path) for path in includes],
                 *['-D'+name+'='+value for name,value in aliases.items()],
                 *[str(ROOT/path) for path in sources],str(folder/'fake.c'),
                 '-o',str(cls.binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:
            raise AssertionError(built.stdout+built.stderr)
        cls.lab_binary=folder/'occupancy.dll'
        subprocess.run(command[:-2]+['-DUTHOME_OCCUPANCY_LAB=1','-Drecv=c3fake_recv',
            '-Dsend=c3fake_send',str(ROOT/'firmware/src/network/occwire.c'),
            '-o',str(cls.lab_binary)],check=True)
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
            '#include <stdint.h>\n#define UTHOME_SETUP_PORT 8443U\n'
            'static const char g_setup_ssid[33]="ap";\n'
            'static const char g_setup_password[64]="password";\n'
            'static const uint8_t g_setup_certificate[]="certificate";\n'
            'static const uint8_t g_setup_key[]="key";\n'
            'static const uint8_t g_setup_token[]="0123456789abcdef0123456789abcdef";\n')
        for wifi,ble in [(1,0),(0,1),(1,1)]:
            compiled=subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror',
                '-pedantic','-c',f'-DUTHOME_NETWORK_WIFI={wifi}',f'-DUTHOME_NETWORK_BLE={ble}',
                *['-I'+str(path) for path in includes],'-I'+str(ROOT/'firmware/app'),
                str(ROOT/'firmware/app/c3net.c'),'-o',str(folder/f'owner-{wifi}-{ble}.o')],
                capture_output=True,text=True)
            if compiled.returncode:
                raise AssertionError(compiled.stdout+compiled.stderr)
        fixture_dir = Path(__file__).resolve().parent
        if str(fixture_dir) not in sys.path:
            sys.path.insert(0, str(fixture_dir))
        from _network_config_fixture import ConfigLibrary
        cls.config_library=ConfigLibrary()
        cls.addClassCleanup(cls.config_library.close)
        cls.next_instance=0

    def setUp(self):
        type(self).next_instance+=1
        path=Path(self.folder.name)/f'instance-{self.next_instance}.dll'
        shutil.copyfile(self.lab_binary if self._testMethodName.startswith("test_esphome") else self.binary,path)
        self.lib=c.CDLL(str(path))
        self.addCleanup(self.unload)
        self.lib.c3fake_drive.argtypes=[c.c_uint64,c.c_int]
        self.lib.c3fake_input.argtypes=[c.c_int,c.c_char_p,c.c_size_t]
        self.lib.c3fake_output.argtypes=[c.c_int,c.c_void_p]
        self.lib.c3fake_output.restype=c.c_size_t
        self.lib.c3wsta_start.argtypes=[c.c_char_p,c.c_char_p]
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

    def output(self,fd=10):
        output=(c.c_uint8*4096)()
        size=self.lib.c3fake_output(fd,output)
        return bytes(output[:size])

    def drive(self,now=0,online=1):
        return self.lib.c3fake_drive(now,online)

    def request(self,path=b'/v1/status',token=b'0123456789abcdef0123456789abcdef'):
        return b'GET '+path+b' HTTP/1.1\r\nHost: esp.local\r\nAuthorization: Bearer '+token+b'\r\n\r\n'

    def configuration_fixture(self):
        from _network_config_fixture import Fixture
        self.config=Fixture(self.config_library)
        self.lib.c3fake_mux.argtypes=[c.c_void_p]
        self.lib.c3fake_mux(c.byref(self.config.mux))
        self.assertEqual(self.lib.c3fake_start(),0)
        self.lib.c3fake_write_limit(512)
        self.clock=0
        return self.config

    def config_request(self,body=b'{"value":1}',id=1,revision=1,expected=0,token=b'op',boot=b'0000000000000001'):
        return (b'POST /v1/configuration HTTP/1.1\r\nHost: node\r\n'
                b'Authorization: Bearer 0123456789abcdef0123456789abcdef\r\n'
                b'Content-Type: application/json\r\nContent-Length: '+str(len(body)).encode()+
                b'\r\nX-Request-ID: '+str(id).encode()+b'\r\nX-Revision: '+str(revision).encode()+
                b'\r\nX-Expected-Revision: '+str(expected).encode()+b'\r\nX-Expected-Boot-ID: '+boot+
                b'\r\nX-Request-Token: '+token+b'\r\n\r\n'+body)

    def exchange_config(self,data,fd=10):
        self.lib.c3fake_accept_next(fd)
        self.clock+=1;self.drive(self.clock)
        self.lib.c3fake_input(fd,data,len(data))
        changes=0
        for _ in range(100):
            self.clock+=1
            changes+=self.drive(self.clock)>0
            if self.lib.c3fake_closed(fd):break
        output=self.output(fd)
        header,body=output.split(b'\r\n\r\n',1)
        self.assertLessEqual(len(output),512)
        return int(header.split()[1]),json.loads(body),changes

    def test_esphome_real_client(self):
        # Mandatory host interoperability check; the socket pump replaces only SDK I/O.
        import asyncio
        from aioesphomeapi import APIClient
        self.assertEqual(self.lib.c3fake_start(),0)
        async def verify():
            states=asyncio.Queue()
            finished=asyncio.Event()
            async def peer(reader,writer):
                self.lib.c3fake_accept_next(10)
                sent=now=0
                try:
                    while not reader.at_eof():
                        try:data=await asyncio.wait_for(reader.read(64),0.01)
                        except asyncio.TimeoutError:data=None
                        if data==b'':break
                        if data:self.lib.c3fake_input(10,data,len(data))
                        now+=10
                        self.drive(now)
                        out=self.output()
                        if len(out)>sent:
                            writer.write(out[sent:]);await writer.drain();sent=len(out)
                        if self.lib.c3fake_closed(10):break
                finally:
                    writer.close()
                    finished.set()
            server=await asyncio.start_server(peer,'127.0.0.1',0)
            async with server:
                client=APIClient('127.0.0.1',server.sockets[0].getsockname()[1],None)
                await client.connect(login=True)
                try:
                    info=await client.device_info()
                    self.assertEqual(info.name,'uthome-010203040506')
                    entities,_=await client.list_entities_services()
                    self.assertEqual([(e.name,e.device_class) for e in entities],
                                     [('Room occupancy','occupancy'),('Radar data fresh','')])
                    self.assertEqual(entities[1].entity_category.value,2)
                    client.subscribe_states(states.put_nowait)
                    occupancy=await states.get()
                    self.assertEqual((occupancy.key,occupancy.missing_state),(entities[0].key,True))
                    initial=await states.get()
                    self.assertEqual((initial.key,initial.state,initial.missing_state),(2,False,False))
                    for mode,expected,missing in [(2,True,False),(1,False,False),(3,False,True)]:
                        self.lib.c3fake_radar(mode)
                        state=await states.get()
                        self.assertEqual((state.key,state.state,state.missing_state),
                                         (entities[0].key,expected,missing))
                        if mode in (2,3):
                            fresh=await states.get()
                            self.assertEqual((fresh.key,fresh.state,fresh.missing_state),
                                             (entities[1].key,not missing,False))
                finally:
                    await client.disconnect()
                await finished.wait()
        asyncio.run(asyncio.wait_for(verify(),15))

    def test_esphome_display_and_mqtt_publisher_share_host_pump(self):
        # Independent socket doubles; no real radio, TLS handshake or broker.
        self.assertEqual(self.lib.c3fake_start(), 0)
        self.assertEqual(self.lib.c3fake_mqtt_start(), 0)
        self.lib.c3fake_handshake(1)
        self.lib.c3fake_mqtt_step.argtypes = [c.c_uint64, c.c_int]
        self.lib.c3mqtt_status.argtypes = [c.c_void_p]
        self.lib.c3fake_accept_next(10)
        self.assertEqual(self.drive(1), 0)
        self.lib.c3fake_input(10, bytes([0, 0, 1]), 3)
        sent_ack = False
        view = (c.c_uint32 * 4)()
        for now in range(2, 250):
            self.assertEqual(self.drive(now), 0)
            self.assertEqual(self.lib.c3fake_mqtt_step(now, 1), 0)
            packet = self.output(0)
            if (not sent_ack and len(packet) >= 2 and packet[0] == 0x10
                    and packet[1] < 128 and len(packet) >= 2 + packet[1]):
                self.lib.c3fake_input(0, bytes([0x20, 2, 0, 0]), 4)
                sent_ack = True
            self.assertEqual(self.lib.c3mqtt_status(view), 0)
            if sent_ack and view[0] == 5 and b'uthome-' in self.output(10):
                break
        self.assertTrue(sent_ack)
        self.assertEqual(view[0], 5)  # NETBROKER_READY
        self.assertIn(b'uthome-', self.output(10))
        self.assertIn(b'online', self.output(0))
        self.assertEqual(self.lib.c3fake_closed(10), 0)

    def test_esphome_occupancy_from_cache_and_reconnect(self):
        self.assertEqual(self.lib.c3fake_start(),0)
        self.lib.c3fake_accept_next(10)
        self.lib.c3fake_write_limit(3)
        now=0
        def step(data=b''):
            nonlocal now
            before=len(self.output())
            if data:self.lib.c3fake_input(10,data,len(data))
            for _ in range(50):  # drain two descriptions even at three bytes per tick
                now+=1
                self.assertEqual(self.drive(now),0)
            return self.output()[before:]
        self.assertEqual(step(b'\x00\x00'),b'')  # fragmented hello
        self.assertIn(b'uthome-010203040506',step(b'\x01'))
        self.assertIn(b'room_occupancy',step(bytes([0,0,11])))
        def packet(key,state,missing):
            return bytes([0,9,21,13,key,0,0,0,16,state,24,missing])
        self.assertEqual(step(bytes([0,0,20])),packet(1,0,1)+packet(2,0,0))
        fresh=False
        for mode,state,missing in [(2,1,0),(1,0,0),(3,0,1),(2,1,0),(4,0,1),(1,0,0),(5,0,1)]:
            self.lib.c3fake_radar(mode)
            expected=packet(1,state,missing)
            if fresh != (not missing):expected+=packet(2,not missing,0)
            self.assertEqual(step(),expected)
            fresh=not missing
        step(bytes([0,126]))  # exceeds receive bound; closes only this client
        self.assertEqual(self.lib.c3fake_closed(10),1)
        self.lib.c3fake_accept_next(10)
        self.assertIn(b'uthome-',step(bytes([0,0,1])))
        self.lib.c3fake_radar(1)
        self.assertEqual(step(bytes([0,0,20])),packet(1,0,0)+packet(2,1,0))
        self.assertEqual(self.lib.c3fake_deleted(),0)  # never instantiated TLS

    def test_https_configuration_state_and_result_fit_max_width_without_io(self):
        f=self.configuration_fixture()
        f.control.last_id=0xffffffff
        f.control.active_revision=f.control.saved_revision=0xffffffff
        f.mux.request_id=0xffffffff;f.mux.token=b'x'*64;f.mux.result=True
        for index,value in enumerate([1,0xffffffff,0xffffffff,0xffffffff,0xffffffff,95]):
            f.mux.reply[index]=value
        before=(bytes(f.control),f.writes,f.syncs)
        status,body,_=self.exchange_config(self.request(b'/v1/configuration'))
        self.assertEqual((status,body['last_request_id'],body['max_body']),(200,0xffffffff,8192))
        query=self.request(b'/v1/configuration/result').replace(b'\r\n\r\n',
            b'\r\nX-Request-ID: 4294967295\r\nX-Request-Token: '+b'x'*64+b'\r\n\r\n')
        status,body,_=self.exchange_config(query,11)
        self.assertEqual((status,body['flags'],body['historical']),(200,95,False))
        self.assertEqual((bytes(f.control),f.writes,f.syncs),before)

    def test_https_configuration_uses_controller_and_exact_retry(self):
        f=self.configuration_fixture()
        status,body,changed=self.exchange_config(self.config_request())
        self.assertEqual((status,body['controller_status'],body['active_revision'],body['saved_revision']),(200,1,1,1))
        self.assertEqual(changed,1)
        self.assertEqual(f.activations,1)
        writes=f.writes
        status,retried,_=self.exchange_config(self.config_request(),11)
        self.assertEqual((status,retried),(200,body))
        self.assertEqual((f.activations,f.writes),(1,writes))
        status,_,_=self.exchange_config(self.config_request(b'{"value":2}'),12)
        self.assertEqual(status,409)
        self.assertEqual(f.activations,1)

    def test_https_full_8192_body_is_streamed_into_existing_rx_storage(self):
        f=self.configuration_fixture()
        data=self.config_request(b'{"value":1}'+b' '*(8192-11))
        self.lib.c3fake_accept_next(10);self.drive(1)
        now=1
        for offset in range(0,len(data),500):
            chunk=data[offset:offset+500]
            self.lib.c3fake_input(10,chunk,len(chunk))
            for _ in range(5):
                now+=1;self.drive(now)
        for _ in range(5):
            now+=1;self.drive(now)
        header,body=self.output().split(b'\r\n\r\n',1)
        self.assertIn(b'200',header)
        self.assertEqual(json.loads(body)['controller_status'],1)
        self.assertEqual((f.activations,f.control.active_size),(1,8192))
        self.assertFalse(f.mux.lease)

    def test_https_auth_rejection_preconditions_and_malformed_body_do_not_save(self):
        f=self.configuration_fixture()
        bad=self.config_request().replace(b'Bearer 0123456789abcdef0123456789abcdef',b'Bearer wrong')
        self.assertEqual(self.exchange_config(bad)[0],401)
        self.assertFalse(f.mux.lease)
        self.assertEqual(self.exchange_config(self.config_request(boot=b'0000000000000002'),11)[0],409)
        status,body,_=self.exchange_config(self.config_request(b'{'),12)
        self.assertEqual((status,body['controller_status']),(422,3))
        self.assertEqual((f.activations,f.writes),(0,0))

    def test_https_partial_upload_disconnect_and_deadline_release_usb_lease(self):
        f=self.configuration_fixture()
        data=self.config_request()[:-5]
        self.lib.c3fake_accept_next(10);self.drive(1)
        self.lib.c3fake_input(10,data,len(data))
        for now in range(2,12):self.drive(now)
        self.assertTrue(f.mux.lease)
        self.drive(12,0)
        self.assertFalse(f.mux.lease)
        self.assertEqual((f.activations,f.writes),(0,0))
        self.lib.c3fake_accept_next(11);self.drive(13)
        self.lib.c3fake_input(11,data,len(data))
        for now in range(14,22):self.drive(now)
        self.assertTrue(f.mux.lease)
        self.drive(10014)
        self.assertFalse(f.mux.lease)
        self.assertEqual(f.control.last_id,0)

    def test_https_result_survives_delivery_loss_usb_query_and_uncertain_save(self):
        from tools.config_transfer.protocol import Command,encode_request
        f=self.configuration_fixture();f.fail_sync=5
        self.lib.c3fake_write_limit(-26752)
        self.lib.c3fake_accept_next(10);self.drive(1)
        data=self.config_request();self.lib.c3fake_input(10,data,len(data))
        for now in range(2,12):self.drive(now)
        self.assertEqual(f.control.last_id,1)
        self.assertFalse(f.mux.lease)
        writes=f.writes
        self.drive(12,0)
        f.usb(encode_request(Command.QUERY,2,0))
        self.lib.c3fake_write_limit(512)
        query=self.request(b'/v1/configuration/result').replace(b'\r\n\r\n',b'\r\nX-Request-ID: 1\r\nX-Request-Token: op\r\n\r\n')
        self.clock=12
        status,body,_=self.exchange_config(query,11)
        self.assertEqual((status,body['controller_status'],body['saved_revision'],body['historical']),(200,2,0,True))
        self.assertEqual((f.activations,f.writes),(1,writes))
        self.assertEqual(self.exchange_config(self.config_request(),12)[0],409)

    def test_https_two_posts_have_one_upload_and_no_coalesced_trailing_apply(self):
        f=self.configuration_fixture()
        data=self.config_request()[:-2]
        self.lib.c3fake_accept_next(10);self.drive(1)
        self.lib.c3fake_input(10,data,len(data))
        for now in range(2,8):self.drive(now)
        self.assertTrue(f.mux.lease)
        self.clock=8
        self.assertEqual(self.exchange_config(self.config_request(token=b'other'),11)[0],503)
        self.assertTrue(f.mux.lease)
        self.drive(self.clock+1,0)
        self.clock+=1
        self.assertFalse(f.mux.lease)
        self.assertEqual(self.exchange_config(self.config_request()+b'x',12)[0],400)
        self.assertEqual(f.activations,0)

    def test_https_usb_arriving_during_upload_waits_then_runs_with_global_id(self):
        from tools.config_transfer.protocol import Command,encode_request,decode_reply
        f=self.configuration_fixture()
        data=self.config_request()
        self.lib.c3fake_accept_next(10);self.drive(1)
        self.lib.c3fake_input(10,data[:-2],len(data)-2)
        for now in range(2,9):self.drive(now)
        self.assertTrue(f.mux.lease)
        f.rx.extend(encode_request(Command.QUERY,2,0))
        before=f.read_calls
        for _ in range(5):f.usb_step()
        self.assertEqual(f.read_calls,before)
        self.assertEqual(f.control.last_id,0)
        self.lib.c3fake_input(10,data[-2:],2)
        for now in range(9,20):self.drive(now)
        self.assertFalse(f.mux.lease)
        self.assertEqual(f.control.last_id,1)
        for _ in range(20):
            f.usb_step()
            if len(f.tx)>=48:break
        reply=decode_reply(bytes(f.tx),command=Command.QUERY,request_id=2,revision=0)
        self.assertEqual(reply.active_revision,1)
        self.assertEqual(f.control.last_id,2)
        self.assertEqual(f.activations,1)

    def test_https_slow_body_progress_does_not_extend_absolute_upload_deadline(self):
        f=self.configuration_fixture()
        data=self.config_request()
        self.lib.c3fake_accept_next(10);self.drive(1)
        self.lib.c3fake_input(10,data[:-5],len(data)-5)
        for now in range(2,9):self.drive(now)
        self.assertTrue(f.mux.lease)
        for now in (2000,4000,6000,8000):
            self.lib.c3fake_input(10,b' ',1);self.drive(now)
            self.assertTrue(f.mux.lease)
        self.drive(10002)
        self.assertFalse(f.mux.lease)
        self.assertEqual((f.control.last_id,f.activations,f.writes),(0,0,0))
        self.assertEqual(f.begin(),200)
        f.abort()

    def test_https_response_deadline_starts_after_controller_work(self):
        f=self.configuration_fixture()
        self.lib.c3fake_accept_next(10);self.drive(1)
        data=self.config_request();self.lib.c3fake_input(10,data,len(data))
        changed=False
        for now in range(2,20):
            if self.drive(now)>0:
                changed=True;break
        self.assertTrue(changed)
        self.assertEqual((f.activations,f.control.saved_revision),(1,1))
        writes=f.writes
        # Model the owner's next clock sample after long synchronous flash work.
        self.drive(20000)
        for now in range(20001,20010):self.drive(now)
        header,body=self.output().split(b'\r\n\r\n',1)
        self.assertEqual(header.split(b'\r\n',1)[0].split()[1],b'200')
        self.assertEqual(json.loads(body)['controller_status'],1)
        self.assertEqual(f.writes,writes)

    def test_https_real_read_dispatch_partial_writes_and_exact_close(self):
        self.assertEqual(self.lib.c3fake_start(),0)
        self.lib.c3fake_accept_next(10)
        self.drive()
        data=self.request(b'/v1/telemetry?source=2')
        self.lib.c3fake_input(10,data,len(data))
        for now in range(1,60):
            self.drive(now)
        headers,body=self.output().split(b'\r\n\r\n')
        self.assertIn(b'200',headers)
        self.assertIn(b'application/octet-stream',headers)
        self.assertEqual(len(body),112)
        self.assertEqual(body[:4],b'\x01\x01\x01\x01')
        self.assertEqual(self.lib.c3fake_closed(10),1)
        self.assertEqual(self.lib.c3fake_deleted(),1)

    def test_https_handshake_deadline_and_capacity_rejection(self):
        self.lib.c3fake_start()
        self.lib.c3fake_handshake(-26880)
        for fd in [10,11,12]:
            self.lib.c3fake_accept_next(fd)
            self.drive(fd)
        self.assertEqual(self.lib.c3fake_closed(12),1)
        self.assertEqual(self.lib.c3fake_deleted(),0)
        self.drive(10012)
        self.assertEqual(self.lib.c3fake_deleted(),2)
        for fd in [10,11,12]:
            self.assertEqual(self.lib.c3fake_closed(fd),1)

    def test_https_offline_setup_failure_and_restart_do_not_double_close(self):
        self.lib.c3fake_start()
        self.lib.c3fake_setup_error(-1)
        self.lib.c3fake_accept_next(10)
        self.drive()
        self.assertEqual(self.lib.c3fake_closed(10),1)
        self.assertEqual(self.lib.c3fake_deleted(),1)
        self.drive(1,0)
        self.drive(2,0)
        self.assertEqual(self.lib.c3fake_closed(10),1)
        self.lib.c3fake_setup_error(0)
        self.lib.c3fake_accept_next(11)
        self.drive(3)
        data=self.request(token=b'wrong')
        self.lib.c3fake_input(11,data,len(data))
        for now in range(4,50):
            self.drive(now)
        self.assertIn(b'401',self.output(11))

    def test_https_write_want_preserves_packet_and_deadline(self):
        self.lib.c3fake_start()
        self.lib.c3fake_accept_next(10)
        self.drive()
        data=self.request()
        self.lib.c3fake_input(10,data,len(data))
        self.drive(1)
        self.lib.c3fake_write_limit(-26752)
        self.drive(2)
        self.assertEqual(self.output(),b'')
        self.lib.c3fake_write_limit(2000)
        self.drive(3)
        body=self.output().split(b'\r\n\r\n')[1]
        self.assertEqual(json.loads(body)['saved_revision'],11)
        self.assertEqual(self.lib.c3fake_closed(10),1)

    def test_wifi_association_ip_loss_and_reconnect(self):
        self.assertEqual(self.lib.c3wsta_start(b'my-ap',b'password'),0)
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

    def test_wifi_timed_out_attempt_waits_for_cancellation(self):
        self.lib.c3wsta_start(b'my-ap',b'password')
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
        (folder/'cert.pem').write_text('-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
        (folder/'key.pem').write_text('-----BEGIN PRIVATE KEY-----\nfixture\n-----END PRIVATE KEY-----\n')
        values=dict(ssid='my-ap',password='password',token='a'*32,
                    certificate_file='cert.pem',key_file='key.pem')
        path=folder/'private.json'
        path.write_text(json.dumps(values))
        return path,values

    def test_esphome_lab_requires_explicit_private_opt_in(self):
        with tempfile.TemporaryDirectory() as temp:
            path,settings=self.configuration(Path(temp))
            plain,_=self.setup.generate(path)
            self.assertRegex(plain,rb'#define UTHOME_OCCUPANCY_LAB +\(0U\)')
            settings['esphome_lab']=True
            path.write_text(json.dumps(settings))
            lab,_=self.setup.generate(path)
            self.assertRegex(lab,rb'#define UTHOME_OCCUPANCY_LAB +\(1U\)')
            self.assertRegex(lab,rb'#define UTHOME_SETUP_PORT +\(6053U\)')
            for invalid in [dict(port=8443),dict(esphome_lab="true")]:
                path.write_text(json.dumps(dict(settings,**invalid)))
                with self.assertRaises(ValueError):self.setup.generate(path)

    def test_esphome_display_and_mqtt_can_be_provisioned_together(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            path, settings = self.configuration(folder)
            ca = folder / 'broker-ca.pem'
            ca.write_bytes(b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
            settings['esphome_lab'] = True
            settings['mqtt'] = dict(address='127.0.0.1', server_name='broker.test',
                                    port=8883, user='device', password='password',
                                    ca_file=ca.name)
            path.write_text(json.dumps(settings))
            header, record = self.setup.generate(path)
            self.assertRegex(header, rb'#define UTHOME_OCCUPANCY_LAB +\(1U\)')
            self.assertRegex(header, rb'#define UTHOME_SETUP_PORT +\(6053U\)')
            self.assertRegex(header, rb'#define UTHOME_SETUP_MQTT +\(1U\)')
            self.assertRegex(header, rb'#define UTHOME_SETUP_MQTT_PORT +\(8883U\)')
            self.assertIn('mqtt_ca_sha256', record)

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
