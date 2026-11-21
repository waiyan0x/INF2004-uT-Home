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
    idf_component_get_property(UTHOME_LWIP_LIB lwip COMPONENT_LIB)
    idf_component_get_property(UTHOME_LWIP_DIR lwip COMPONENT_DIR)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/port/freertos/sys_arch.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
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
