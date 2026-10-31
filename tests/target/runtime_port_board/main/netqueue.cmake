# Optional owned queue adaptation. Vendor files and revisions remain intact.
# All aliases preserve the pinned 32-bit FreeRTOS C ABI. The real target build
# verifies their linked addresses and the absence of the vendor queue object.
set(UTHOME_QUEUE_ALIASES
    xQueueGenericCreate=c3nq_create
    xQueueGenericCreateStatic=c3nq_static
    xQueueCreateCountingSemaphore=c3nq_counting
    xQueueCreateCountingSemaphoreStatic=c3nq_counting_static
    xQueueCreateMutex=c3nq_mutex
    xQueueCreateMutexStatic=c3nq_mutex_static
    xQueueGenericSend=c3nq_send
    xQueueGenericSendFromISR=c3nq_send_isr
    xQueueReceive=c3nq_receive
    xQueueReceiveFromISR=c3nq_receive_isr
    xQueuePeek=c3nq_peek
    xQueuePeekFromISR=c3nq_peek_isr
    xQueueSemaphoreTake=c3nq_take
    xQueueGiveFromISR=c3nq_give_isr
    xQueueTakeMutexRecursive=c3nq_take_recursive
    xQueueGiveMutexRecursive=c3nq_give_recursive
    vQueueDelete=c3nq_delete
    uxQueueMessagesWaiting=c3nq_waiting
    uxQueueMessagesWaitingFromISR=c3nq_waiting
    uxQueueSpacesAvailable=c3nq_spaces
    xQueueIsQueueEmptyFromISR=c3nq_empty_isr
    xQueueReset=c3nq_reset
    xQueueGenericReset=c3nq_generic_reset
    xTaskGetSchedulerState=c3nq_scheduler_state
)

set(UTHOME_EVENT_ALIASES
    xEventGroupCreate=c3event_create
    xEventGroupCreateStatic=c3event_static
    vEventGroupDelete=c3event_delete
    xEventGroupSetBits=c3event_set
    xEventGroupSetBitsFromISR=c3event_set_isr
    xEventGroupClearBits=c3event_clear
    xEventGroupClearBitsFromISR=c3event_clear_isr
    xEventGroupGetBits=c3event_get
    xEventGroupGetBitsFromISR=c3event_get
    xEventGroupWaitBits=c3event_wait
    xEventGroupSync=c3event_sync
)

if(UTHOME_STATIC_NET_QUEUES)
    idf_component_get_property(UTHOME_SHIM_LIB freertos_shim COMPONENT_LIB)
    idf_component_get_property(UTHOME_SHIM_DIR freertos_shim COMPONENT_DIR)
    get_target_property(UTHOME_SHIM_SOURCES ${UTHOME_SHIM_LIB} SOURCES)
    set(UTHOME_QUEUE_REMOVED 0)
    set(UTHOME_EVENT_REMOVED 0)
    foreach(source IN LISTS UTHOME_SHIM_SOURCES)
        get_filename_component(source_name "${source}" NAME)
        if(source_name STREQUAL "shim_queue.c")
            list(REMOVE_ITEM UTHOME_SHIM_SOURCES "${source}")
            math(EXPR UTHOME_QUEUE_REMOVED "${UTHOME_QUEUE_REMOVED}+1")
        endif()
        if(source_name STREQUAL "shim_evtgrp.c")
            list(REMOVE_ITEM UTHOME_SHIM_SOURCES "${source}")
            math(EXPR UTHOME_EVENT_REMOVED "${UTHOME_EVENT_REMOVED}+1")
        endif()
    endforeach()
    if(NOT UTHOME_QUEUE_REMOVED EQUAL 1)
        message(FATAL_ERROR "Expected exactly one pinned shim_queue.c input")
    endif()
    if(NOT UTHOME_EVENT_REMOVED EQUAL 1)
        message(FATAL_ERROR "Expected exactly one pinned shim_evtgrp.c input")
    endif()
    set_property(TARGET ${UTHOME_SHIM_LIB} PROPERTY SOURCES "${UTHOME_SHIM_SOURCES}")
    set_property(SOURCE "${UTHOME_SHIM_DIR}/shim_port.c"
        TARGET_DIRECTORY ${UTHOME_SHIM_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        __wrap_xTaskGetSchedulerState=c3nq_legacy_scheduler
        __real_knl_init_object=c3nq_bootstrap)
    # Rename the native definition in its one compilation unit. A defsym to
    # knl_init_object would itself be affected by --wrap and recurse at boot.
    set_property(SOURCE "${UTHOME_NATIVE_ROOT}/kernel/tkernel/tkinit.c"
        TARGET_DIRECTORY ${UTHOME_KERNEL_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        knl_init_object=c3nq_native_tables)
    foreach(pair IN LISTS UTHOME_QUEUE_ALIASES UTHOME_EVENT_ALIASES)
        string(REPLACE "=" ";" parts "${pair}")
        list(GET parts 0 original)
        list(GET parts 1 owned)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--undefined=${owned}"
            "-Wl,--wrap=${original}"
            "-Wl,--defsym=__wrap_${original}=${owned}")
    endforeach()

    # Replace the weak upstream pool provider without changing vendor sources.
    get_target_property(UTHOME_KERNEL_SOURCES ${UTHOME_KERNEL_LIB} SOURCES)
    set(UTHOME_START_REMOVED 0)
    foreach(source IN LISTS UTHOME_KERNEL_SOURCES)
        get_filename_component(source_name "${source}" NAME)
        if(source_name STREQUAL "mtk_startup.c")
            list(REMOVE_ITEM UTHOME_KERNEL_SOURCES "${source}")
            math(EXPR UTHOME_START_REMOVED "${UTHOME_START_REMOVED}+1")
        endif()
    endforeach()
    if(NOT UTHOME_START_REMOVED EQUAL 1)
        message(FATAL_ERROR "Expected exactly one pinned weak startup input")
    endif()
    set_property(TARGET ${UTHOME_KERNEL_LIB} PROPERTY SOURCES "${UTHOME_KERNEL_SOURCES}")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--undefined=c3boot_start"
        "-Wl,--undefined=knl_main"
        "-Wl,--undefined=knl_lowmem_top"
        "-Wl,--undefined=knl_lowmem_limit"
        "-Wl,--defsym=__wrap_esp_startup_start_app=c3boot_start"
        "-Wl,--defsym=c3boot_native_main=knl_main"
        "-Wl,--defsym=gp_c3boot_begin=knl_lowmem_top"
        "-Wl,--defsym=gp_c3boot_end=knl_lowmem_limit")
endif()
