idf_component_register(SRCS "../discovery/discuart.c" "../discovery/disci2c.c" "../discovery/cfgdisc.c" "../config/cfgpump.c" "../hal/c3usb.c"
                       "../hal/c3store.c" "../app/c3app.c" "../config/jsonscan.c" "../config/cfgjson.c" "../config/cfgplan.c" "../config/cfgdev.c" "../config/cfgrule.c" "../automation/rulecalc.c" "../config/cfgbind.c" "../hal/c3bind.c"
                       "main.c" "../runtime/tkport.c"
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
                       INCLUDE_DIRS "../discovery" "../automation" "../services" "../app" "../config" "../runtime" "../include" "../hal/include"
                                    "../hal" "../drivers" "../third_party/bmp3"
                                    "../../source/device/include" "../../source/device/ser"
                       REQUIRES esp_app_format mtkernel esp_rom esp_partition spi_flash esp_hal_usb esp_hal_i2c esp_hal_gpspi
                                esp_hal_rmt esp_hal_ledc esp_timer espressif__cjson)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UTHOME_RUNTIME_UTKERNEL=1
    UTHOME_CONFIGURATION_APP=$<BOOL:${UTHOME_CONFIGURATION_APP}>)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

# IDF appends its own language and warning flags after target options.
# Source options occur last, so these are the effective owned-code profiles.
set(UTHOME_WARNINGS -Wall -Wextra -Werror -Werror=extra
    -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations)
set_source_files_properties("main.c" "../runtime/tkport.c"
    PROPERTIES COMPILE_OPTIONS "-std=c11;${UTHOME_WARNINGS}")
set_source_files_properties("../discovery/discuart.c" "../discovery/disci2c.c" "../discovery/cfgdisc.c" "../config/cfgpump.c" "../hal/c3store.c"
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

# Fault injection exists only in the acceptance image, never the application.
if(NOT UTHOME_CONFIGURATION_APP)
    target_sources(${COMPONENT_LIB} PRIVATE "uartcheck.c")
    set_source_files_properties("uartcheck.c"
        PROPERTIES COMPILE_OPTIONS "-std=c99;${UTHOME_WARNINGS};-fno-inline")
    set_property(SOURCE "../hal/c3uart.c" APPEND PROPERTY COMPILE_DEFINITIONS
        tk_srea_dev=uartcheck_read)
endif()
