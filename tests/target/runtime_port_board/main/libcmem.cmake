if(UTHOME_NETWORK_WIFI AND UTHOME_STATIC_NET_QUEUES AND CONFIG_LIBC_PICOLIBC)
    idf_component_get_property(libc_dir esp_libc COMPONENT_DIR)
    idf_component_get_property(libc_lib esp_libc COMPONENT_LIB)
    set(init_source "${libc_dir}/src/picolibc/picolibc_init.c")
    set(init_copy "${CMAKE_CURRENT_BINARY_DIR}/libc/picolibc_init.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${init_source}")
    file(READ "${init_source}" init_text)
    string(SHA256 init_hash "${init_text}")
    if(NOT init_hash STREQUAL "81a9b63ef3d8368d1bc575d1a8696da1e2779a7c35e87f7ea5ac5d2ac855394d")
        message(FATAL_ERROR "Review the changed picolibc startup environment initialization")
    endif()
    # Native setenv can realloc the initial table on a failed-growth retry.
    # Keep the original ownership whenever that archive member is linked.
    string(REPLACE "#include <stdlib.h>" "#include <stdlib.h>\n#pragma weak setenv" init_text "${init_text}")
    string(REPLACE "    environ = malloc(sizeof(char*));" [=[    static char *empty_environment[1];
    environ = empty_environment;
    if (setenv != NULL) {
        environ = malloc(sizeof(char*));
    }]=] init_text "${init_text}")
    file(CONFIGURE OUTPUT "${init_copy}" CONTENT "${init_text}" @ONLY)
    get_target_property(libc_sources ${libc_lib} SOURCES)
    list(FILTER libc_sources EXCLUDE REGEX "(^|/)picolibc_init[.]c$")
    set_property(TARGET ${libc_lib} PROPERTY SOURCES "${libc_sources};${init_copy}")
endif()
