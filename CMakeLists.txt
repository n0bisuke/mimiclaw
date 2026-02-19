cmake_minimum_required(VERSION 3.16)

# ESP-IDF 5.5.0+ changed relative-path resolution for SDKCONFIG_DEFAULTS.
# Set absolute paths here so CMake always resolves them correctly.
# PlatformIO passes -DBUILD_ATOMCLAW=1 or -DBUILD_MIMICLAW=1 via cmake_extra_args.
# idf.py users can still override with -DSDKCONFIG_DEFAULTS=... on the command line.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
    if(BUILD_MIMICLAW)
        set(SDKCONFIG_DEFAULTS
            "${_d}/sdkconfig.defaults"
            "${_d}/sdkconfig.defaults.esp32s3"
        )
    else()
        # Default: AtomClaw
        set(SDKCONFIG_DEFAULTS
            "${_d}/sdkconfig.defaults"
            "${_d}/sdkconfig.defaults.esp32s3"
            "${_d}/sdkconfig.defaults.atomclaw"
        )
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atomclaw)
