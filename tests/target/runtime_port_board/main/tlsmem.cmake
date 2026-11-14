# Fixed ESP-TLS records. This does not replace mbedTLS or lwIP allocation.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    if(NOT CONFIG_ESP_TLS_USING_MBEDTLS OR CONFIG_ESP_TLS_CUSTOM_STACK OR
       CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS OR CONFIG_ESP_TLS_SERVER_SESSION_TICKETS)
        message(FATAL_ERROR "Fixed ESP-TLS records require mbedTLS without session-ticket storage")
    endif()
    idf_component_get_property(UTHOME_TLS_LIB esp-tls COMPONENT_LIB)
    idf_component_get_property(UTHOME_TLS_DIR esp-tls COMPONENT_DIR)
    # These two vendor units allocate only supported context/name storage on
    # the selected path. Other allocation shapes fail; no heap fallback.
    foreach(source IN ITEMS esp_tls.c esp_tls_mbedtls.c)
        set_property(SOURCE "${UTHOME_TLS_DIR}/${source}"
            TARGET_DIRECTORY ${UTHOME_TLS_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
            calloc=c3tls_alloc free=c3tls_free strndup=c3tls_dup)
    endforeach()
    foreach(pair IN ITEMS
            esp_tls_internal_event_tracker_create=c3tls_error_create
            esp_tls_internal_event_tracker_destroy=c3tls_error_destroy
            esp_tls_internal_event_tracker_capture=c3tls_error_capture
            esp_tls_get_and_clear_error_type=c3tls_error_get)
        string(REPLACE "=" ";" parts "${pair}")
        list(GET parts 0 original)
        list(GET parts 1 owned)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--undefined=${owned}"
            "-Wl,--wrap=${original}"
            "-Wl,--defsym=__wrap_${original}=${owned}")
    endforeach()
endif()
