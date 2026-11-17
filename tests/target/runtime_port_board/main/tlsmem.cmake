# Fixed ESP-TLS records. This does not replace mbedTLS or lwIP allocation.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    if(CONFIG_MBEDTLS_DYNAMIC_BUFFER)
        message(FATAL_ERROR
            "Dynamic TLS buffers are unsupported: pinned SDK loses partial nonblocking record headers")
    endif()
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
    # Setup and hostname setting select fixed storage. Other allocations keep
    # their SDK path; both failure-free and zeroizing cleanup must be bound.
    idf_component_get_property(UTHOME_MBED_DIR mbedtls COMPONENT_DIR)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/library/ssl_tls.c"
        TARGET_DIRECTORY mbedtls APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3tout_alloc mbedtls_free=c3tout_free
        mbedtls_zeroize_and_free=c3tout_wipe)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--undefined=c3tout_setup"
        "-Wl,--undefined=mbedtls_ssl_setup"
        "-Wl,--wrap=mbedtls_ssl_setup"
        "-Wl,--defsym=__wrap_mbedtls_ssl_setup=c3tout_setup"
        "-Wl,--defsym=c3tout_native_setup=__real_mbedtls_ssl_setup"
        "-Wl,--undefined=c3tout_set_name"
        "-Wl,--undefined=mbedtls_ssl_set_hostname"
        "-Wl,--wrap=mbedtls_ssl_set_hostname"
        "-Wl,--defsym=__wrap_mbedtls_ssl_set_hostname=c3tout_set_name"
        "-Wl,--defsym=c3tout_native_name=__real_mbedtls_ssl_set_hostname"
        "-Wl,--undefined=c3tout_start"
        "-Wl,--undefined=mbedtls_ssl_start_renegotiation"
        "-Wl,--wrap=mbedtls_ssl_start_renegotiation"
        "-Wl,--defsym=__wrap_mbedtls_ssl_start_renegotiation=c3tout_start"
        "-Wl,--defsym=c3tout_native_start=__real_mbedtls_ssl_start_renegotiation"
        "-Wl,--undefined=c3tout_renegotiate"
        "-Wl,--undefined=mbedtls_ssl_renegotiate"
        "-Wl,--wrap=mbedtls_ssl_renegotiate"
        "-Wl,--defsym=__wrap_mbedtls_ssl_renegotiate=c3tout_renegotiate"
        "-Wl,--defsym=c3tout_native_renegotiate=__real_mbedtls_ssl_renegotiate"
        "-Wl,--undefined=c3tout_certificate"
        "-Wl,--undefined=mbedtls_ssl_parse_certificate"
        "-Wl,--wrap=mbedtls_ssl_parse_certificate"
        "-Wl,--defsym=__wrap_mbedtls_ssl_parse_certificate=c3tout_certificate"
        "-Wl,--defsym=c3tout_native_certificate=__real_mbedtls_ssl_parse_certificate")
endif()
