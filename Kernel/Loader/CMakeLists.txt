
cmake_minimum_required(VERSION 3.16)
project(NativeElfLoader CXX)

add_executable(native_elf_loader loader.cpp)

target_link_libraries(native_elf_loader PRIVATE KernelUserlandShared)

set_target_properties(native_elf_loader PROPERTIES POSITION_INDEPENDENT_CODE OFF)
set_property(TARGET native_elf_loader APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)

target_compile_options(native_elf_loader PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-pic -fno-pie -m32)
target_link_options(native_elf_loader PRIVATE -nostdlib -no-pie -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
