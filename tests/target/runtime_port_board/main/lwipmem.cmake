# Redirect both ends of socket, TCP, UDP, message, ARP queue, multicast and timeout record lifetimes.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    idf_component_get_property(UTHOME_EVENT_LIB esp_event COMPONENT_LIB)
    idf_component_get_property(UTHOME_EVENT_DIR esp_event COMPONENT_DIR)
    # The private cleanup event shares its byte count with ordinary IP events.
    # Only this SDK call site has the event base needed to reserve cleanup slots.
    set(event_source "${UTHOME_EVENT_DIR}/esp_event.c")
    set(event_copy "${CMAKE_CURRENT_BINARY_DIR}/event/esp_event.c")
    file(READ "${event_source}" event_text)
    set(old_body "post.data.ptr = calloc(1, event_data_size)")
    string(SHA256 event_hash "${event_text}")
    if(NOT event_hash STREQUAL "0a31dfec97db91b3d8fbc5780998533715cbe255b59bff6b8f32df2218133936")
        message(FATAL_ERROR "SDK event body allocation changed")
    endif()
    string(REPLACE "${old_body}" "post.data.ptr = c3ereg_body(event_data_size, event_base == esp_event_handler_cleanup)" event_text "${event_text}")
    file(CONFIGURE OUTPUT "${event_copy}" CONTENT "${event_text}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${event_source}")
    get_target_property(event_sources ${UTHOME_EVENT_LIB} SOURCES)
    list(FILTER event_sources EXCLUDE REGEX "(^|/)esp_event[.]c$")
    set_property(TARGET ${UTHOME_EVENT_LIB} PROPERTY SOURCES "${event_sources};${event_copy}")
    set_property(SOURCE "${event_copy}" TARGET_DIRECTORY ${UTHOME_EVENT_LIB} APPEND PROPERTY COMPILE_OPTIONS "-include" "${CMAKE_CURRENT_LIST_DIR}/../hal/c3ereg.h")
    set_property(SOURCE "${event_copy}"
        TARGET_DIRECTORY ${UTHOME_EVENT_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        heap_caps_calloc=c3ereg_alloc free=c3ereg_free)
    # Substitute only registry node storage in the pinned pthread unit.
    idf_component_get_property(pthread_dir pthread COMPONENT_DIR)
    idf_component_get_property(pthread_lib pthread COMPONENT_LIB)
    set(key_source "${pthread_dir}/pthread_local_storage.c")
    set(key_copy "${CMAKE_CURRENT_BINARY_DIR}/pthread/pthread_local_storage.c")
    file(READ "${key_source}" key_text)
    string(SHA256 key_hash "${key_text}")
    if(NOT key_hash STREQUAL "838b9ea24df5493cc648248110bc5ce2e28c98bbad945ac7cfd70912977cc378")
        message(FATAL_ERROR "Pinned pthread registry layout or lifetime changed")
    endif()
    string(REPLACE "malloc(sizeof(key_entry_t))" "c3lsem_reserve()" key_text "${key_text}")
    string(REPLACE "SLIST_REMOVE(&s_keys, entry, key_entry_t_, next);
        free(entry);"
        "SLIST_REMOVE(&s_keys, entry, key_entry_t_, next);
        c3lsem_unlinked(entry);"
        key_text "${key_text}")
    file(CONFIGURE OUTPUT "${key_copy}" CONTENT "extern void *c3lsem_reserve(void);
extern void c3lsem_unlinked(const void *);
@key_text@" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${key_source}")
    get_target_property(pthread_sources ${pthread_lib} SOURCES)
    list(FILTER pthread_sources EXCLUDE REGEX "(^|/)pthread_local_storage[.]c$")
    set_property(TARGET ${pthread_lib} PROPERTY SOURCES "${pthread_sources};${key_copy}")
    set_property(SOURCE "${key_copy}" TARGET_DIRECTORY ${pthread_lib} APPEND PROPERTY INCLUDE_DIRECTORIES "${pthread_dir}")
    # Preserve native lock, destroy, and failure paths; substitute only record ownership.
    set(mutex_source "${pthread_dir}/pthread.c")
    set(mutex_copy "${CMAKE_CURRENT_BINARY_DIR}/pthread/pthread.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mutex_source}")
    file(READ "${mutex_source}" mutex_text)
    string(SHA256 mutex_hash "${mutex_text}")
    if(NOT mutex_hash STREQUAL "3423df09f2a291e76abf2cb34a26b5fde21a22bd514563f1526d44cdafa8da15")
        message(FATAL_ERROR "Review the changed SDK pthread mutex allocation and release paths")
    endif()
    string(REPLACE "malloc(sizeof(esp_pthread_mutex_t))" "c3lsem_mutex_reserve()" mutex_text "${mutex_text}")
    string(REPLACE "free(mux);" "c3lsem_mutex_release(mux);" mutex_text "${mutex_text}")
    file(CONFIGURE OUTPUT "${mutex_copy}" CONTENT "extern void *c3lsem_mutex_reserve(void);
extern void c3lsem_mutex_release(const void *);
@mutex_text@" @ONLY)
    get_target_property(pthread_sources ${pthread_lib} SOURCES)
    list(FILTER pthread_sources EXCLUDE REGEX "(^|/)pthread[.]c$")
    set_property(TARGET ${pthread_lib} PROPERTY SOURCES "${pthread_sources};${mutex_copy}")
    set_property(SOURCE "${mutex_copy}" TARGET_DIRECTORY ${pthread_lib} APPEND PROPERTY
        INCLUDE_DIRECTORIES "${pthread_dir}")
    idf_component_get_property(UTHOME_LWIP_LIB lwip COMPONENT_LIB)
    idf_component_get_property(UTHOME_LWIP_DIR lwip COMPONENT_DIR)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/port/freertos/sys_arch.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pthread_key_create=c3lsem_key
        sys_thread_sem_get=c3lsem_native_get sys_thread_sem_init=c3lsem_native_init
        sys_thread_sem_deinit=c3lsem_native_deinit)
    foreach(operation IN ITEMS get init deinit)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--undefined=c3lsem_${operation}"
            "-Wl,--defsym=sys_thread_sem_${operation}=c3lsem_${operation}")
    endforeach()
    foreach(source IN ITEMS api/api_lib.c api/api_msg.c api/netbuf.c api/tcpip.c core/udp.c core/ipv4/etharp.c core/ipv4/igmp.c core/tcp.c core/tcp_out.c core/timeouts.c)
        set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/${source}"
            TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
            memp_malloc=c3lrec_alloc memp_free=c3lrec_free)
    endforeach()
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/tcp_out.c"
                        "${UTHOME_LWIP_DIR}/lwip/src/core/ipv4/etharp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pbuf_alloc=c3pkt_alloc)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/ipv4/etharp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pbuf_clone=c3pkt_clone)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/ipv4/icmp.c"
                        "${UTHOME_LWIP_DIR}/lwip/src/core/ipv4/ip4_frag.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pbuf_alloc=c3pkt_frame)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/udp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pbuf_alloc=c3pkt_aux)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/tcp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        tcp_split_unsent_seg=c3pkt_split)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/ipv4/dhcp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        mem_malloc=c3dhcp_alloc mem_free=c3dhcp_free pbuf_alloc=c3dhcp_packet)
    idf_component_get_property(UTHOME_NETIF_LIB esp_netif COMPONENT_LIB)
    idf_component_get_property(UTHOME_NETIF_DIR esp_netif COMPONENT_DIR)
    set_property(SOURCE "${UTHOME_NETIF_DIR}/esp_netif_objects.c"
        TARGET_DIRECTORY ${UTHOME_NETIF_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        calloc=c3iface_alloc free=c3iface_free)
    set_property(SOURCE "${UTHOME_NETIF_DIR}/lwip/esp_netif_lwip.c"
        TARGET_DIRECTORY ${UTHOME_NETIF_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        calloc=c3iface_alloc free=c3iface_free strdup=c3iface_dup)
    set_property(SOURCE "${UTHOME_NETIF_DIR}/lwip/netif/wlanif.c"
        TARGET_DIRECTORY ${UTHOME_NETIF_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        esp_pbuf_allocate=c3rx_alloc pbuf_alloc=c3pkt_aux)
    # SDK archives are scanned after the owned main/radio archives. Retain the
    # record adapter and its native exclusion boundary before that scan.
    foreach(owned IN ITEMS c3lrec_alloc c3lrec_free c3pkt_alloc c3pkt_aux c3pkt_frame c3pkt_clone c3pkt_split c3rx_alloc c3dhcp_alloc c3dhcp_free c3dhcp_packet c3iface_alloc c3iface_free c3iface_dup c3ereg_alloc c3ereg_body c3ereg_free c3bport_enter c3bport_leave)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=${owned}")
    endforeach()
endif()

# The station has one WPA event loop and one task-dispatched, one-shot timer.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES AND UTHOME_CONFIGURATION_APP AND NOT CONFIG_ESP_TIMER_PROFILING)
    foreach(component IN ITEMS esp_timer wpa_supplicant)
        idf_component_get_property(dir ${component} COMPONENT_DIR)
        idf_component_get_property(lib ${component} COMPONENT_LIB)
        if(component STREQUAL "esp_timer")
            set(rel "src/esp_timer.c")
            set(expected "5063132e2fa243d69b85b9bc1c93bb19d79110b265da07b5e6a4b779f66c563e")
        else()
            set(rel "port/eloop.c")
            set(expected "2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac")
        endif()
        set(source "${dir}/${rel}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" body)
        string(SHA256 actual "${body}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "Review WPA timer lifetime against changed ${rel}")
        endif()
        if(component STREQUAL "esp_timer")
            string(APPEND body [=[

extern void c3wpatmr_callback(void *);
struct esp_timer g_c3wpatmr = { .callback = c3wpatmr_callback };
]=])
        else()
            string(PREPEND body [=[#include "esp_timer.h"
#include "esp_err.h"
extern struct esp_timer g_c3wpatmr;
void c3wpatmr_callback(void *);
]=])
            string(REPLACE "    ETSTimer eloop_timer;" "" body "${body}")
            string(REPLACE "static void eloop_run_timer(" "void c3wpatmr_callback(" body "${body}")
            string(REPLACE "os_timer_disarm(&eloop.eloop_timer);" "(void)esp_timer_stop(&g_c3wpatmr);" body "${body}")
            foreach(delay IN ITEMS 100 ms)
                string(REPLACE "os_timer_arm(&eloop.eloop_timer, ${delay}, 0);"
                    "ESP_ERROR_CHECK(esp_timer_start_once(&g_c3wpatmr, 1000ULL * ${delay}));" body "${body}")
            endforeach()
            string(REPLACE "    os_timer_setfn(&eloop.eloop_timer, (ETSTimerFunc *)eloop_run_timer, NULL);" "" body "${body}")
            string(REPLACE "    os_timer_done(&eloop.eloop_timer);" "" body "${body}")
        endif()
        set(copy "${CMAKE_CURRENT_BINARY_DIR}/wpatimer/${rel}")
        file(CONFIGURE OUTPUT "${copy}" CONTENT "${body}" @ONLY)
        get_target_property(sources ${lib} SOURCES)
        list(FILTER sources EXCLUDE REGEX "(^|/)${rel}$")
        set_property(TARGET ${lib} PROPERTY SOURCES "${sources};${copy}")
        get_filename_component(source_dir "${source}" DIRECTORY)
        set_property(SOURCE "${copy}" TARGET_DIRECTORY ${lib} APPEND PROPERTY INCLUDE_DIRECTORIES "${source_dir}")
    endforeach()
endif()
