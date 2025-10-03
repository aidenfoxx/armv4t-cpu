cmake_minimum_required(VERSION 3.11)

project(armv4t-cpu LANGUAGES C)

file(GLOB SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

add_library(armv4t-cpu STATIC ${SRC})

target_include_directories(armv4t-cpu
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
