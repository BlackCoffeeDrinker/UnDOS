
cmake_minimum_required(VERSION 3.16)
project(NativeElfLoader CXX)

add_executable(native_elf_loader loader.cpp)

target_compile_options(native_elf_loader PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -m32)
target_link_options(native_elf_loader PRIVATE -nostdlib -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
