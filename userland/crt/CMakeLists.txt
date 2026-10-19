cmake_minimum_required(VERSION 3.16)
project(undoscrt C CXX ASM)

add_library(undoscrt STATIC
    start.S
    heap.c
    process.cpp
    stdio.c
    string.c
    libc/backend.c
    libc/stdlib.c
    libc/string.c
    libc/stdio.c
    libc/format.c
)

target_include_directories(undoscrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(undoscrt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libc/include)

target_link_libraries(undoscrt PUBLIC KernelUserlandShared)

set_target_properties(undoscrt PROPERTIES POSITION_INDEPENDENT_CODE OFF)
set_target_properties(undoscrt PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
# Generated syscall declarations use C23 enums/constexpr and GNU inline assembly.
set_source_files_properties(libc/backend.c stdio.c PROPERTIES COMPILE_OPTIONS "-std=gnu23")

target_compile_options(undoscrt PRIVATE
    -ffreestanding
    -nostdlib
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>
    -fno-pic
    -fno-pie
    -m32
)
