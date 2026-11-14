# NimBLE requires the SDK/toolchain BSD sys/queue.h. Do not add mtkernel here.
# The owned c3bport interface crosses into the main component at link time.
idf_component_register(SRCS "../../hal/c3ble.c" "../../hal/c3gatt.c"
    "../../hal/c3mqtt.c" "../../hal/c3lbox.c" "../../hal/c3tls.c" "../../hal/c3wsta.c" "../../hal/c3https.c" "../../hal/c3gsvc.c"
    "../../network/netbroker.c" "../../network/netmqtt.c" "../../network/netretry.c"
    "../../network/nethttp.c" "../../network/netread.c" "../../network/netble.c"
    INCLUDE_DIRS "../../config" "../../hal" "../../runtime" "../../network" "../../services" "../../include"
    REQUIRES bt esp_timer esp_hw_support esp_wifi esp_event esp_netif esp-tls lwip
    PRIV_REQUIRES uthome_task)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UTHOME_MQTT_VALUE_BYTES=112 UTHOME_MQTT_PACKET_BYTES=384
    UTHOME_STATIC_NET_QUEUES=$<BOOL:${UTHOME_STATIC_NET_QUEUES}>
    UTHOME_NETWORK_WIFI=$<BOOL:${UTHOME_NETWORK_WIFI}>
    UTHOME_NETWORK_BLE=$<BOOL:${UTHOME_NETWORK_BLE}>
    UTHOME_BLE_PROBE=$<BOOL:${UTHOME_BLE_PROBE}>
    UTHOME_BLE_GATT_PROBE=$<BOOL:${UTHOME_BLE_GATT_PROBE}>)
if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES)
    idf_component_get_property(UTHOME_TLS_DIR esp-tls COMPONENT_DIR)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${UTHOME_TLS_DIR}/private_include")
endif()
set_source_files_properties("../../hal/c3ble.c" "../../hal/c3gatt.c" "../../hal/c3mqtt.c" "../../hal/c3lbox.c" "../../hal/c3tls.c" "../../hal/c3wsta.c" "../../hal/c3https.c" "../../hal/c3gsvc.c" PROPERTIES COMPILE_OPTIONS
    "-std=c99;-Wall;-Wextra;-Werror;-Werror=extra;-Werror=unused-function;-Werror=unused-variable;-Werror=unused-but-set-variable;-Werror=deprecated-declarations")
set_property(SOURCE "../../hal/c3ble.c" "../../hal/c3gatt.c" "../../hal/c3mqtt.c" "../../hal/c3lbox.c" "../../hal/c3tls.c" "../../hal/c3wsta.c" "../../hal/c3https.c" "../../hal/c3gsvc.c" APPEND PROPERTY COMPILE_DEFINITIONS
    asm=__asm__)

# Keep portable transport codecs with their SDK callers. A later archive must
# not depend on unreferenced objects in the already-scanned main archive.
# These sources include no SDK assembly and receive no asm spelling alias.
set_source_files_properties("../../network/netbroker.c" "../../network/netmqtt.c" "../../network/netretry.c"
    "../../network/nethttp.c" "../../network/netread.c" "../../network/netble.c"
    PROPERTIES COMPILE_OPTIONS
    "-std=c99;-Wall;-Wextra;-Werror;-Werror=extra;-Werror=unused-function;-Werror=unused-variable;-Werror=unused-but-set-variable;-Werror=deprecated-declarations")
