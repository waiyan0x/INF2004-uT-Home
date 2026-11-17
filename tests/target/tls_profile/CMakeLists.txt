cmake_minimum_required(VERSION 3.20)
project(uthome_tls_profile C)

# Host-only software crypto; never included by the ESP build.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(MBEDTLS_SSL_IN_CONTENT_LEN=${TLS_INPUT}
                        MBEDTLS_SSL_OUT_CONTENT_LEN=${TLS_OUTPUT}
                        MBEDTLS_USER_CONFIG_FILE="tlsconf.h")
include_directories(BEFORE "${MBEDTLS_SOURCE}/../port/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory("${MBEDTLS_SOURCE}" mbedtls)
add_library(tlspeer SHARED tlspeer.c)
target_compile_options(tlspeer PRIVATE -std=c99 -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(tlspeer PRIVATE mbedtls)
if(MINGW)
    target_link_options(tlspeer PRIVATE -static-libgcc)
endif()
set_target_properties(tlspeer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
