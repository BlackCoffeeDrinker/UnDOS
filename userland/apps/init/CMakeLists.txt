cmake_minimum_required(VERSION 3.16)
project(init CXX ASM)

add_executable(init main.cpp)

target_include_directories(init PRIVATE ${CMAKE_SOURCE_DIR}/userland)
target_link_libraries(init PRIVATE undoscrt KernelUserlandShared)

set_target_properties(init PROPERTIES POSITION_INDEPENDENT_CODE OFF)

target_compile_options(init PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-pic -fno-pie -m32)
target_link_options(init PRIVATE -nostdlib -no-pie -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
