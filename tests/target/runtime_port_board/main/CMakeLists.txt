
idf_component_register(SRCS "../discovery/discuart.c" "../discovery/disci2c.c" "../discovery/cfgdisc.c" "../config/cfgpump.c" "../hal/c3usb.c"
                       "../hal/c3store.c" "../config/cfgpart.c" "../app/c3app.c" "../config/jsonscan.c" "../config/cfgjson.c" "../config/cfgplan.c" "../config/cfgdev.c" "../config/cfgrule.c" "../automation/rulecalc.c" "../config/cfgbind.c" "../hal/c3bind.c"
                       "../network/netring.c" "../network/netwait.c" "../network/netpool.c" "../network/netevent.c" "../hal/c3event.c" "../hal/c3boot.c" "../hal/c3nq.c" "../hal/c3task.c" "../hal/c3wifi.c" "../hal/c3bport.c"
                       "main.c" "nqcheck.c" "../runtime/tkport.c" "../runtime/stackmon.c" "../runtime/workcost.c" "../runtime/workmeter.c" "../hal/c3clock.c" "../hal/c3cost.c" "../services/costview.c" "../runtime/memaudit.c" "../hal/c3audit.c"
                       "../runtime/activate.c" "../runtime/shared.c" "../services/sampcache.c" "../services/statusmon.c" "../services/telemetry.c" "../services/oledview.c"
                       "../runtime/component.c" "../messaging/msgbus.c"
                       "../health/health.c" "../health/watchdog.c"
                       "../config/drvreg.c" "../hal/i2cbus.c" "../hal/i2cown.c" "../hal/c3i2c.c"
                       "../drivers/bmp388.c" "../drivers/bmpinst.c"
                       "../hal/pinpool.c" "../hal/spibus.c" "../hal/spiown.c"
                       "../hal/c3pin.c" "../hal/c3spi.c"
                       "../drivers/ssd1306.c" "../drivers/oledinst.c"
                       "../hal/uartio.c" "../hal/c3uart.c" "../hal/c3umem.c"
                       "../drivers/ldparse.c" "../drivers/ld2450.c" "../drivers/ldinst.c"
                       "../hal/pulseio.c" "../hal/pwmio.c"
                       "../drivers/hc_sr04.c" "../drivers/hcinst.c" "../drivers/pwminst.c"
                       "../hal/c3pulse.c" "../hal/c3pwm.c"
                       "../config/cfgbytes.c" "../config/cfgwire.c"
                       "../config/cfgstore.c" "../config/cfgctrl.c"
                       "../third_party/bmp3/bmp3.c"
                       INCLUDE_DIRS "../network" "../discovery" "../automation" "../services" "../app" "../config" "../runtime" "../include" "../hal/include"
                                    "../hal" "../drivers" "../third_party/bmp3"
                                    "../../source/device/include" "../../source/device/ser"
                       LDFRAGMENTS "audit.lf" "netqueue.lf"
                       REQUIRES uthome_radio bt esp_wifi esp_event esp_app_format mtkernel freertos_shim esp_rom esp_partition spi_flash esp_hal_usb esp_hal_i2c esp_hal_gpspi
                                esp_hal_rmt esp_hal_ledc esp_timer espressif__cjson)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UTHOME_RUNTIME_UTKERNEL=1
    UTHOME_CONFIGURATION_APP=$<BOOL:${UTHOME_CONFIGURATION_APP}>
    UTHOME_STATIC_NET_QUEUES=$<BOOL:${UTHOME_STATIC_NET_QUEUES}>
    UTHOME_WIFI_PROBE=$<BOOL:${UTHOME_WIFI_PROBE}>
    UTHOME_BLE_PROBE=$<BOOL:${UTHOME_BLE_PROBE}>
    UTHOME_BLE_GATT_PROBE=$<BOOL:${UTHOME_BLE_GATT_PROBE}>
    UTHOME_MODEL_PROBE=$<BOOL:${UTHOME_MODEL_PROBE}>
    UTHOME_NATIVE_UART_FIXTURE=$<BOOL:${UTHOME_NATIVE_UART_FIXTURE}>)
if(UTHOME_CONFIGURATION_APP AND UTHOME_NATIVE_UART_FIXTURE)
    message(FATAL_ERROR "Application cannot include native UART fixture")
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

# IDF appends its own language and warning flags after target options.
# Source options occur last, so these are the effective owned-code profiles.
set(UTHOME_WARNINGS -Wall -Wextra -Werror -Werror=extra
    -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations)
set_source_files_properties("main.c" "../runtime/tkport.c"
    PROPERTIES COMPILE_OPTIONS "-std=c11;${UTHOME_WARNINGS}")
set_source_files_properties("../discovery/discuart.c" "../discovery/disci2c.c" "../discovery/cfgdisc.c" "../config/cfgpump.c" "../hal/c3store.c" "../config/cfgpart.c"
    "../runtime/stackmon.c" "../runtime/workcost.c" "../runtime/workmeter.c" "../hal/c3clock.c" "../hal/c3cost.c" "../services/costview.c" "../runtime/memaudit.c" "../hal/c3audit.c"
    "../hal/c3usb.c" "../app/c3app.c" "../config/jsonscan.c" "../config/cfgjson.c" "../config/cfgplan.c" "../config/cfgdev.c" "../config/cfgrule.c" "../automation/rulecalc.c" "../config/cfgbind.c" "../hal/c3bind.c"
    "../runtime/activate.c" "../runtime/shared.c" "../services/sampcache.c" "../services/statusmon.c" "../services/telemetry.c" "../services/oledview.c"
    "../runtime/component.c" "../messaging/msgbus.c" "../health/health.c"
    "../health/watchdog.c" "../config/drvreg.c" "../hal/i2cbus.c" "../hal/i2cown.c"
    "../drivers/bmp388.c" "../drivers/bmpinst.c"
    "../hal/pinpool.c" "../hal/spibus.c" "../hal/spiown.c"
    "../drivers/ssd1306.c" "../drivers/oledinst.c"
    "../hal/uartio.c" "../hal/c3uart.c" "../hal/c3umem.c" "../drivers/ldparse.c"
    "../drivers/ld2450.c" "../drivers/ldinst.c"
    "../hal/pulseio.c" "../hal/pwmio.c"
    "../drivers/hc_sr04.c" "../drivers/hcinst.c" "../drivers/pwminst.c"
    "../config/cfgbytes.c" "../config/cfgwire.c"
    "../config/cfgstore.c" "../config/cfgctrl.c"
    PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_WARNINGS}")
# Pinned external LL headers use GNU typeof. The adapter's owned source is C99;
# these exact translation units need the vendor header's GNU99 dialect.
set_source_files_properties("../hal/c3i2c.c" "../hal/c3pin.c" "../hal/c3spi.c"
    "../hal/c3pulse.c" "../hal/c3pwm.c"
    PROPERTIES COMPILE_OPTIONS "-std=gnu99;${UTHOME_WARNINGS}")

# Keep direct Ninja/IDF rebuilds behind the same mandatory repository check.
# Verify both copied and original inputs before checking repository policy.
# Changed inputs require a fresh qualification build, never a silent reuse.
if(NOT UTHOME_SOURCE_ROOT OR NOT UTHOME_CHECK_PYTHON)
    message(FATAL_ERROR "Build this acceptance project with tools/target/qualify_kernel.py")
endif()
add_custom_target(uthome_c_standard
    COMMAND "${UTHOME_CHECK_PYTHON}"
            "${UTHOME_SOURCE_ROOT}/tools/target/qualify_kernel.py"
            verify-snapshot --project-dir "${CMAKE_CURRENT_LIST_DIR}/.."
    COMMAND "${UTHOME_CHECK_PYTHON}"
            "${UTHOME_SOURCE_ROOT}/tools/c_standard/check.py"
            --root "${UTHOME_SOURCE_ROOT}"
    VERBATIM
    USES_TERMINAL)
add_dependencies(${COMPONENT_LIB} uthome_c_standard)

# The binary endpoint requires an exclusive, quiet console configuration.
if(UTHOME_CONFIGURATION_APP)
    if(NOT CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG OR
       NOT CONFIG_LOG_DEFAULT_LEVEL EQUAL 0 OR
       NOT CONFIG_LOG_MAXIMUM_LEVEL EQUAL 0)
        message(FATAL_ERROR "Configuration application requires native USB and disabled SDK logs")
    endif()
endif()

# Adapt only the two pinned native translation units; never rewrite dependency
# files or globally wrap the kernel allocator. Declarations in tk/syslib.h are
# renamed with the call sites, preserving the exact size_t/void* ABI.
# TARGET_DIRECTORY makes source properties visible in the kernel target scope.
idf_component_get_property(UTHOME_KERNEL_LIB mtkernel COMPONENT_LIB)
idf_component_get_property(UTHOME_KERNEL_DIR mtkernel COMPONENT_DIR)
set(UTHOME_NATIVE_ROOT "${UTHOME_KERNEL_DIR}/../..")
set_property(SOURCE "${UTHOME_NATIVE_ROOT}/device/ser/ser.c"
    TARGET_DIRECTORY ${UTHOME_KERNEL_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
    Kmalloc=c3umem_serial_alloc Kfree=c3umem_serial_free)
set_property(SOURCE "${UTHOME_NATIVE_ROOT}/device/common/drvif/msdrvif.c"
    TARGET_DIRECTORY ${UTHOME_KERNEL_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
    Kmalloc=c3umem_record_alloc Kfree=c3umem_record_free)

# The pinned SDK's ordinary guards enumerate partitions and allocate iterators.
# Keep protection active with an owned guard that permits only the verified
# configuration range. This build does not authorize NVS/OTA writes.
idf_component_get_property(UTHOME_FLASH_LIB spi_flash COMPONENT_LIB)
idf_component_get_property(UTHOME_FLASH_DIR spi_flash COMPONENT_DIR)
set_property(SOURCE "${UTHOME_FLASH_DIR}/spi_flash_os_func_app.c"
    TARGET_DIRECTORY ${UTHOME_FLASH_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
    esp_partition_is_flash_region_writable=c3store_region_allowed
    esp_partition_main_flash_region_safe=c3store_region_allowed)

# Count successful SDK heap events from reset, without allocating in hooks.
if(NOT CONFIG_HEAP_USE_HOOKS)
    message(FATAL_ERROR "Allocation evidence requires SDK heap hooks")
endif()
if(NOT UTHOME_CONFIGURATION_APP)
    # The application references capture/seal directly. The fixture has no
    # telemetry owner; force archive extraction instead of relying on weak
    # SDK hooks or linker-expression aliases to pull in the audit object.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=c3audit_alloc")
endif()
idf_component_get_property(UTHOME_HEAP_LIB heap COMPONENT_LIB)
idf_component_get_property(UTHOME_HEAP_DIR heap COMPONENT_DIR)
set_property(SOURCE "${UTHOME_HEAP_DIR}/heap_caps_base.c"
    TARGET_DIRECTORY ${UTHOME_HEAP_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
    esp_heap_trace_alloc_hook=c3audit_alloc
    esp_heap_trace_free_hook=c3audit_free)

# Preserve vendor definitions under owned native names; all external callers
# pass through a counted wrapper. Link aliases avoid unapproved C spellings.
function(uthome_audit_creator source syscall name)
    set_property(SOURCE "${UTHOME_NATIVE_ROOT}/kernel/tkernel/${source}.c"
        TARGET_DIRECTORY ${UTHOME_KERNEL_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        "${syscall}=c3audit_native_${name}")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=${syscall}"
        "-Wl,--defsym=__wrap_${syscall}=c3audit_create_${name}")
endfunction()
uthome_audit_creator(task_manage tk_cre_tsk task)
uthome_audit_creator(mutex tk_cre_mtx mutex)
uthome_audit_creator(semaphore tk_cre_sem sem)
uthome_audit_creator(eventflag tk_cre_flg flag)
uthome_audit_creator(mailbox tk_cre_mbx mailbox)
uthome_audit_creator(messagebuf tk_cre_mbf message)
uthome_audit_creator(mempfix tk_cre_mpf fixed_pool)
uthome_audit_creator(mempool tk_cre_mpl pool)
uthome_audit_creator(rendezvous tk_cre_por port)
uthome_audit_creator(time_calls tk_cre_cyc cycle)
uthome_audit_creator(time_calls tk_cre_alm alarm)

foreach(allocator malloc calloc realloc free)
    set_property(SOURCE "${UTHOME_NATIVE_ROOT}/kernel/tkernel/memory.c"
        TARGET_DIRECTORY ${UTHOME_KERNEL_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        "knl_I${allocator}=c3audit_native_${allocator}")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=knl_I${allocator}"
        "-Wl,--defsym=__wrap_knl_I${allocator}=c3audit_k${allocator}")
endforeach()

# This isolated test drives UART pins and requires no external sender/receiver.
# The ordinary runtime fixture can run with the reviewed peripherals attached.
if(UTHOME_NATIVE_UART_FIXTURE)
    target_sources(${COMPONENT_LIB} PRIVATE "uartcheck.c")
    set_source_files_properties("uartcheck.c"
        PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_WARNINGS};-fno-inline")
    set_property(SOURCE "../hal/c3uart.c" APPEND PROPERTY COMPILE_DEFINITIONS
        tk_srea_dev=uartcheck_read)
endif()

# Compile the owned adapter in every build; enable its startup hook and link
# substitutions only for the explicit experimental queue mode.
set_source_files_properties("nqcheck.c" "../network/netring.c" "../network/netwait.c"
    "../network/netpool.c" "../network/netevent.c" "../hal/c3event.c" "../hal/c3boot.c" "../hal/c3nq.c" "../hal/c3task.c" "../hal/c3wifi.c" "../hal/c3bport.c"
    PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_WARNINGS}")
include("${CMAKE_CURRENT_LIST_DIR}/netqueue.cmake")

# The project-owned fixed-stack contract is consumed only by the patched shim
# in isolated radio builds. Keep the vendor source unchanged and expose just
# this reviewed interface to its target.
if(UTHOME_WIFI_PROBE OR UTHOME_BLE_PROBE)
    if(NOT UTHOME_SHIM_LIB)
        message(FATAL_ERROR "Static radio task stacks require the shim target")
    endif()
    target_include_directories(${UTHOME_SHIM_LIB} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../hal")
endif()

set_property(SOURCE "../hal/c3task.c" APPEND PROPERTY COMPILE_DEFINITIONS
    UTHOME_C3TASK_TARGET=1)

if(UTHOME_WIFI_PROBE OR UTHOME_BLE_PROBE OR UTHOME_MODEL_PROBE)
    # Keep the USB core's complete fixed RAM/code footprint in the diagnostic.
    # It is not run concurrently and this is not a combined-system acceptance.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=c3app_run")
endif()
if(UTHOME_BLE_PROBE)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--undefined=c3bport_enter")
    if(NOT CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY)
        message(FATAL_ERROR "BLE diagnostic requires the SDK flash controller")
    endif()
    # The legacy patch writes ROM dispatch-table cells and must not be linked
    # with the flash controller. Remove only this input and extraction flag.
    get_target_property(UTHOME_BLE_SOURCES ${UTHOME_SHIM_LIB} SOURCES)
    set(UTHOME_BLE_PATCH_REMOVED 0)
    foreach(source IN LISTS UTHOME_BLE_SOURCES)
        get_filename_component(source_name "${source}" NAME)
        if(source_name STREQUAL "shim_ble_patch.c")
            list(REMOVE_ITEM UTHOME_BLE_SOURCES "${source}")
            math(EXPR UTHOME_BLE_PATCH_REMOVED "${UTHOME_BLE_PATCH_REMOVED}+1")
        endif()
    endforeach()
    get_target_property(UTHOME_BLE_LINK ${UTHOME_SHIM_LIB} INTERFACE_LINK_OPTIONS)
    set(UTHOME_BLE_EXTRACT "-Wl,-u,shim_ble_patch_modules_funcs")
    if(NOT UTHOME_BLE_PATCH_REMOVED EQUAL 1 OR NOT UTHOME_BLE_EXTRACT IN_LIST UTHOME_BLE_LINK)
        message(FATAL_ERROR "Pinned legacy BLE patch inputs differ")
    endif()
    list(REMOVE_ITEM UTHOME_BLE_LINK "${UTHOME_BLE_EXTRACT}")
    set_property(TARGET ${UTHOME_SHIM_LIB} PROPERTY SOURCES "${UTHOME_BLE_SOURCES}")
    set_property(TARGET ${UTHOME_SHIM_LIB} PROPERTY INTERFACE_LINK_OPTIONS "${UTHOME_BLE_LINK}")
endif()

# Pinned SDK FreeRTOS headers spell inline assembly with the GNU alias `asm`.
# Keep owned C99 unchanged and scope the spelling adaptation to this adapter's
# external headers. The kernel header follows SDK headers to retain its guards.
set_property(SOURCE "../hal/c3wifi.c" APPEND PROPERTY COMPILE_DEFINITIONS
    asm=__asm__)

# Frozen experimental model and test vectors are discarded from other images.
# The qualifier checks this link boundary; no application advice is enabled.
target_sources(${COMPONENT_LIB} PRIVATE
    "../model/aifrozen.c" "../model/aicases.c" "../model/aiprobe.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "../model")
set_source_files_properties("../model/aifrozen.c" "../model/aicases.c" "../model/aiprobe.c"
    PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_WARNINGS};-fstack-usage")
