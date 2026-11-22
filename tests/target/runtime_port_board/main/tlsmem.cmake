# Fixed ESP-TLS records. This does not replace mbedTLS or lwIP allocation.
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    if(CONFIG_MBEDTLS_DYNAMIC_BUFFER)
        message(FATAL_ERROR
            "Dynamic TLS buffers are unsupported: pinned SDK loses partial nonblocking record headers")
    endif()
    if(NOT CONFIG_ESP_TLS_USING_MBEDTLS OR CONFIG_ESP_TLS_CUSTOM_STACK OR
       CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS OR CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS OR
       CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS OR CONFIG_ESP_TLS_SERVER_SESSION_TICKETS)
        message(FATAL_ERROR "Fixed MQTT TLS requires mbedTLS with session tickets disabled at both layers")
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
    set_property(SOURCE "${UTHOME_TLS_DIR}/esp_tls.c"
        TARGET_DIRECTORY ${UTHOME_TLS_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        lwip_getaddrinfo=c3mqtt_resolve lwip_freeaddrinfo=c3mqtt_release
        select=lwip_select)
    set_property(SOURCE "${UTHOME_TLS_DIR}/esp_tls_mbedtls.c"
        TARGET_DIRECTORY ${UTHOME_TLS_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_x509_crt_parse=c3mqtt_trust)
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
    if(CONFIG_MBEDTLS_HARDWARE_SHA OR CONFIG_MBEDTLS_HARDWARE_AES OR
       NOT CONFIG_ESP_WIFI_MBEDTLS_CRYPTO)
        message(FATAL_ERROR "Fixed TLS crypto requires software SHA/AES and the reviewed WPA provider")
    endif()
    # Preserve the ESP32-C3 radio's optimized WPA2 passphrase calculation.
    # MQTT SHA-256 and HMAC keep their software PSA providers.
    idf_component_get_property(wpa_lib wpa_supplicant COMPONENT_LIB)
    idf_component_get_property(wpa_dir wpa_supplicant COMPONENT_DIR)
    set(wpa_crypto "${wpa_dir}/esp_supplicant/src/crypto")
    target_sources(${wpa_lib} PRIVATE "${wpa_crypto}/fastpsk.c")
    set_property(SOURCE "${wpa_crypto}/crypto_mbedtls.c" TARGET_DIRECTORY ${wpa_lib}
                 APPEND PROPERTY COMPILE_DEFINITIONS CONFIG_FAST_PSK)
    idf_component_get_property(UTHOME_MBED_DIR mbedtls COMPONENT_DIR)
    set(cipher_source "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/src/cipher_wrap.c")
    set(cipher_copy "${CMAKE_CURRENT_BINARY_DIR}/cipher/cipher_wrap.c")
    file(READ "${cipher_source}" cipher_text)
    string(SHA256 cipher_hash "${cipher_text}")
    if(NOT cipher_hash STREQUAL "77a61ccd7dbfbe145bd2efcbe5cb2290f5d18fff7e25d88df256c45a218a771a")
        message(FATAL_ERROR "Pinned AES context lifetime changed")
    endif()
    string(REPLACE "mbedtls_aes_context *aes = mbedtls_calloc(1, sizeof(mbedtls_aes_context))"
                   "mbedtls_aes_context *aes = c3aes_take()" cipher_text "${cipher_text}")
    string(REPLACE "mbedtls_aes_free((mbedtls_aes_context *) ctx);\n    mbedtls_free(ctx);"
                   "mbedtls_aes_free((mbedtls_aes_context *) ctx);\n    c3aes_drop(ctx);" cipher_text "${cipher_text}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cipher")
    file(WRITE "${cipher_copy}" "${cipher_text}")
    get_target_property(cipher_sources builtin SOURCES)
    list(FILTER cipher_sources EXCLUDE REGEX "(^|/)cipher_wrap[.]c$")
    set_property(TARGET builtin PROPERTY SOURCES "${cipher_sources};${cipher_copy}")
    get_filename_component(cipher_dir "${cipher_source}" DIRECTORY)
    set_property(SOURCE "${cipher_copy}" TARGET_DIRECTORY builtin APPEND PROPERTY INCLUDE_DIRECTORIES "${cipher_dir}")
    set_property(SOURCE "${cipher_copy}" TARGET_DIRECTORY builtin APPEND PROPERTY COMPILE_OPTIONS
                 "-include" "${CMAKE_CURRENT_LIST_DIR}/../hal/c3aes.h")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=c3aes_take" "-Wl,--undefined=c3aes_drop")
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/library/ssl_tls.c"
        TARGET_DIRECTORY mbedtls APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3tout_alloc mbedtls_free=c3tout_free
        mbedtls_zeroize_and_free=c3tout_wipe
        psa_key_derivation_input_bytes=c3tout_prf_input
        psa_key_derivation_input_key=c3tout_prf_key
        psa_import_key=c3peer_tls_import
        mbedtls_x509_crt_parse_der_nocopy=c3peer_der
        mbedtls_x509_crt_verify_restartable=c3peer_verify
        mbedtls_pk_parse_subpubkey=c3peer_copy
        mbedtls_ssl_handshake_client_step=c3peer_client_step)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/library/ssl_tls12_client.c"
        TARGET_DIRECTORY mbedtls APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_pk_verify_restartable=c3peer_signature psa_generate_key=c3peer_generate)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/src/bignum.c"
        TARGET_DIRECTORY builtin APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_limbs mbedtls_free=c3peer_drop
        mbedtls_zeroize_and_free=c3peer_wipe)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/src/ecp.c"
        TARGET_DIRECTORY builtin APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_points mbedtls_free=c3peer_points_free)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c"
        TARGET_DIRECTORY builtin APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_key mbedtls_free=c3peer_key_free)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/library/x509.c"
        TARGET_DIRECTORY mbedx509 APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_name_alloc)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/utilities/asn1parse.c"
        TARGET_DIRECTORY utilities APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_free=c3tout_node_free)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/library/x509_crt.c"
        TARGET_DIRECTORY mbedx509 APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_alloc mbedtls_free=c3peer_free)
    set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/core/psa_crypto.c"
        TARGET_DIRECTORY tfpsacrypto APPEND PROPERTY COMPILE_DEFINITIONS
        mbedtls_calloc=c3peer_psa_alloc mbedtls_free=c3peer_psa_free
        mbedtls_zeroize_and_free=c3peer_psa_wipe)
    foreach(source IN ITEMS pk_ecc.c pk_wrap.c)
        set_property(SOURCE "${UTHOME_MBED_DIR}/mbedtls/tf-psa-crypto/extras/${source}"
            TARGET_DIRECTORY extras APPEND PROPERTY COMPILE_DEFINITIONS
            psa_import_key=c3peer_import)
    endforeach()
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
