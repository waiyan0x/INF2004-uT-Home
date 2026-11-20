# Redirect both ends of each socket, TCP PCB and segment record lifetime.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    idf_component_get_property(UTHOME_LWIP_LIB lwip COMPONENT_LIB)
    idf_component_get_property(UTHOME_LWIP_DIR lwip COMPONENT_DIR)
    foreach(source IN ITEMS api/api_lib.c api/api_msg.c api/netbuf.c core/tcp.c core/tcp_out.c)
        set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/${source}"
            TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
            memp_malloc=c3lrec_alloc memp_free=c3lrec_free)
    endforeach()
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/tcp_out.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        pbuf_alloc=c3pkt_alloc)
    set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/core/tcp.c"
        TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        tcp_split_unsent_seg=c3pkt_split)
    # SDK archives are scanned after the owned main/radio archives. Retain the
    # record adapter and its native exclusion boundary before that scan.
    foreach(owned IN ITEMS c3lrec_alloc c3lrec_free c3pkt_alloc c3pkt_split c3bport_enter c3bport_leave)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=${owned}")
    endforeach()
endif()
