cmake_minimum_required(VERSION 3.16)
project(term CXX ASM)

add_executable(term main.cpp)

target_include_directories(term PRIVATE ${CMAKE_SOURCE_DIR}/userland)
target_link_libraries(term PRIVATE undoscrt KernelUserlandShared)

set_target_properties(term PROPERTIES POSITION_INDEPENDENT_CODE OFF)

target_compile_options(term PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-pic -fno-pie -m32)
target_link_options(term PRIVATE -nostdlib -no-pie -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
