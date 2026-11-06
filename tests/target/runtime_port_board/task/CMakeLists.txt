# Project-owned fixed task-stack service shared by the application and the
# patched FreeRTOS compatibility shim. It depends only on the native kernel,
# so consumers never need to depend on main or uthome_radio.
idf_component_register(
    SRCS "../../hal/c3task.c"
    INCLUDE_DIRS "../../hal"
    PRIV_REQUIRES mtkernel)

if(UTHOME_STATIC_TASKS AND
   (UTHOME_WIFI_PROBE OR UTHOME_NETWORK_WIFI) AND
   (UTHOME_BLE_PROBE OR UTHOME_NETWORK_BLE))
    message(FATAL_ERROR
        "The reviewed static task pool supports one radio profile at a time")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UTHOME_C3TASK_TARGET=1
    UTHOME_STATIC_TASKS=$<BOOL:${UTHOME_STATIC_TASKS}>
    UTHOME_NETWORK_WIFI=$<BOOL:${UTHOME_NETWORK_WIFI}>
    UTHOME_NETWORK_BLE=$<BOOL:${UTHOME_NETWORK_BLE}>
    UTHOME_WIFI_PROBE=$<BOOL:${UTHOME_WIFI_PROBE}>
    UTHOME_BLE_PROBE=$<BOOL:${UTHOME_BLE_PROBE}>)

set(UTHOME_TASK_WARNINGS -Wall -Wextra -Werror -Werror=extra
    -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations)
set_source_files_properties("../../hal/c3task.c"
    PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_TASK_WARNINGS}")
