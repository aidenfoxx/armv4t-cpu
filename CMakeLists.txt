cmake_minimum_required(VERSION 3.11)

project(armv4t-cpu
    VERSION 0.1.0
    LANGUAGES C
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(armv4t-cpu STATIC 
    src/arm.c
    src/thumb.c
    src/cpu.c
)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set_property(TARGET armv4t-cpu PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

target_compile_features(armv4t-cpu PRIVATE c_std_99)
target_compile_options(armv4t-cpu PRIVATE -Wno-parentheses)

target_include_directories(armv4t-cpu
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)
