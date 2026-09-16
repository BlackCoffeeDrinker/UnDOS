cmake_minimum_required(VERSION 3.16)
project(undoscrt C CXX ASM)

add_library(undoscrt STATIC
    start.S
    heap.cpp
    process.cpp
    stdio.cpp
    string.cpp
)

target_include_directories(undoscrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(undoscrt PUBLIC KernelUserlandShared)

set_target_properties(undoscrt PROPERTIES POSITION_INDEPENDENT_CODE OFF)

target_compile_options(undoscrt PRIVATE
    -ffreestanding
    -nostdlib
    -fno-exceptions
    -fno-rtti
    -fno-pic
    -fno-pie
    -m32
)
