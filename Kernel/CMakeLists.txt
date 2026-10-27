cmake_minimum_required(VERSION 3.27)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(UnDOSKernel VERSION 0.0.1 LANGUAGES C CXX ASM)
include(../BuildSupport/Project.cmake)
include(../BuildSupport/AddDriver.cmake)
include(../BuildSupport/AddHal.cmake)
include(../BuildSupport/AddSubsystem.cmake)

add_subdirectory(../SDK/KStd KStd)
add_subdirectory(API)
add_subdirectory(../ABI ABI)
add_subdirectory(HAL)
add_subdirectory(Source)
add_subdirectory(Drivers)
add_subdirectory(Subsystems)
add_subdirectory(Loader)
add_library(bootinfo_layout_probe OBJECT EXCLUDE_FROM_ALL
    ../Boot/GrubDirect/bootinfo_layout_probe.cpp)
target_link_libraries(bootinfo_layout_probe PRIVATE
    KernelHeaders libkcpp undos_warnings undos_options)
target_compile_options(native_elf_loader PRIVATE -march=i386)
target_sources(native_elf_loader PRIVATE "${UNDOS_ROOT}/ABI/abi_note.S")
target_link_options(native_elf_loader PRIVATE
    -Wl,--export-dynamic -Wl,--hash-style=sysv -Wl,--no-dynamic-linker)

# Image assembly consumes this manifest, never guesses paths in CMake build trees.
set(artifacts "")
foreach(entry IN ITEMS
    "kernel|boot/kernel.elf" "hal_x86_old|boot/hal_x86_old.elf"
    "native_elf_loader|System/ELoader.elf"
    "bus_isa|System/Drivers/bus_isa.elf" "disk_ata|System/Drivers/disk_ata.elf"
    "volmgr|System/Drivers/volmgr.elf" "fs_fat|System/Drivers/fs_fat.elf"
    "gfx_vga|System/Drivers/gfx_vga.elf" "gfx_cirrus|System/Drivers/gfxcirr.elf"
    "input_i8042|System/Drivers/i8042.elf" "input_gameport|System/Drivers/gameport.elf"
    "serial_uart|System/Drivers/serial.elf")
    string(REPLACE "|" ";" pair "${entry}")
    list(GET pair 0 target)
    list(GET pair 1 destination)
    list(APPEND artifacts
        "\"${destination}\": \"$<TARGET_FILE:${target}>\"")
endforeach()
get_property(subsystems GLOBAL PROPERTY UNDOS_SUBSYSTEMS)
set(registry "")
foreach(target IN LISTS subsystems)
    get_property(filename GLOBAL PROPERTY UNDOS_SUBSYSTEM_${target}_FILE)
    get_property(start GLOBAL PROPERTY UNDOS_SUBSYSTEM_${target}_START)
    list(APPEND artifacts "\"System/Subsys/${filename}\": \"$<TARGET_FILE:${target}>\"")
    list(APPEND registry "\"${target}:${start}:/System/Subsys/${filename}\"")
endforeach()
list(JOIN artifacts ",\n" artifacts_json)
list(JOIN registry ",\n" registry_json)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/artifacts-$<CONFIG>.json"
    CONTENT "{ \"files\": {${artifacts_json}}, \"subsystems\": [${registry_json}] }\n")
