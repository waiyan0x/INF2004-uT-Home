cmake_minimum_required(VERSION 3.18)
# qualify_kernel.py stages this project beside the pinned kernel source.
set(EXTRA_COMPONENT_DIRS ../source/components)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;parser.defaults")
if(UTHOME_CONFIGURATION_APP)
    list(APPEND SDKCONFIG_DEFAULTS "configuration.defaults")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(uthome_runtime_port)
