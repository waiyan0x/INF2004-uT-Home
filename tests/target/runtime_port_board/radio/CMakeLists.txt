# NimBLE requires the SDK/toolchain BSD sys/queue.h. Do not add mtkernel here.
# The owned c3bport interface crosses into the main component at link time.
idf_component_register(SRCS "../../hal/c3ble.c" "../../hal/c3gatt.c"
    INCLUDE_DIRS "../../hal" "../../runtime"
    REQUIRES bt esp_timer esp_hw_support)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UTHOME_BLE_PROBE=$<BOOL:${UTHOME_BLE_PROBE}>
    UTHOME_BLE_GATT_PROBE=$<BOOL:${UTHOME_BLE_GATT_PROBE}>)
set_source_files_properties("../../hal/c3ble.c" "../../hal/c3gatt.c" PROPERTIES COMPILE_OPTIONS
    "-std=c99;-Wall;-Wextra;-Werror;-Werror=extra;-Werror=unused-function;-Werror=unused-variable;-Werror=unused-but-set-variable;-Werror=deprecated-declarations")
set_property(SOURCE "../../hal/c3ble.c" "../../hal/c3gatt.c" APPEND PROPERTY COMPILE_DEFINITIONS
    asm=__asm__)
