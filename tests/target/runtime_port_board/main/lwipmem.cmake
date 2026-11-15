# Only these pinned units allocate/free NETCONN and NETBUF records. Packet
# payloads, PCBs and other lwIP pools remain separate allocation work.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    idf_component_get_property(UTHOME_LWIP_LIB lwip COMPONENT_LIB)
    idf_component_get_property(UTHOME_LWIP_DIR lwip COMPONENT_DIR)
    foreach(source IN ITEMS api_lib.c api_msg.c netbuf.c)
        set_property(SOURCE "${UTHOME_LWIP_DIR}/lwip/src/api/${source}"
            TARGET_DIRECTORY ${UTHOME_LWIP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
            memp_malloc=c3lrec_alloc memp_free=c3lrec_free)
    endforeach()
    # SDK archives are scanned after the owned main/radio archives. Retain the
    # record adapter and its native exclusion boundary before that scan.
    foreach(owned IN ITEMS c3lrec_alloc c3lrec_free c3bport_enter c3bport_leave)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=${owned}")
    endforeach()
endif()
