cmake_minimum_required(VERSION 3.27)

include(cmake/PreventInSourceBuilds.cmake)
option(UNDOS_SDK_ONLY "Build only application SDK libraries and installation targets" OFF)
if (UNDOS_SDK_ONLY AND TESTING_ONLY)
    message(FATAL_ERROR "UNDOS_SDK_ONLY and TESTING_ONLY are mutually exclusive")
endif ()

##
# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(UnDOS
        VERSION 0.0.1
)

enable_language(ASM)

set(tools /usr/local/cross)


#######################################################################################################################
## Global Project targeting
#######################################################################################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

#######################################################################################################################
## CMake Module configuration
#######################################################################################################################
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
        ${PROJECT_SOURCE_DIR}/cmake)

include(CMakeDependentOption)
include(CheckCXXCompilerFlag)
include(ExternalProject)
include(FetchContent)

if (NOT UNDOS_SDK_ONLY)
    include(cmake/CPM.cmake)
endif ()
include(cmake/StandardProjectSettings.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/AddDriver.cmake)
include(cmake/AddHal.cmake)
include(cmake/AddSubsystem.cmake)

#######################################################################################################################
## Make options
#######################################################################################################################
option(UNDOS_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)

#######################################################################################################################
## Make dummy targets for propagating compiler options
#######################################################################################################################
add_library(undos_warnings INTERFACE)
add_library(undos_options INTERFACE)

undos_set_project_warnings(
        undos_warnings
        ${UNDOS_WARNINGS_AS_ERRORS}
        ""
        ""
        ""
        "")

target_compile_features(undos_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
if (NOT TESTING_ONLY)
    # Don't link with anything by default unless we say so
    set(CMAKE_C_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C Libraries")
    set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C++ Libraries")

    target_compile_options(undos_options INTERFACE
            -m32
            -march=i386
            -nostdlib
            -fno-builtin
            -fno-rtti
            -fno-exceptions
            -fno-stack-protector
            -ffreestanding
            -Wall
            -Wextra
    )
    target_link_options(undos_options INTERFACE
            -nostdlib
            -nostartfiles
            -nodefaultlibs
            -nostdinc
            -nolibc
            -Wl,--oformat=elf32-i386
            -Wl,--nmagic
            -L ${tools}/lib/gcc/i686-elf/14.1.0/)
else ()
endif ()

#######################################################################################################################
## Generate Syscalls
#######################################################################################################################
find_program(PYTHON3_EXECUTABLE NAMES python3 REQUIRED)

add_custom_target(undos-syscalls-gen
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/create-syscalls.py tools/syscalls.txt kernel_userland_shared/include/undos/syscalls userland/ kernel/src/syscalls
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS tools/syscalls.txt tools/gfxsyscall.txt tools/usersyscall.txt tools/abisyscall.txt tools/netsyscall.txt
        COMMENT "Generating syscalls from tools/syscalls.txt (includes tools/gfxsyscall.txt)"
)

#######################################################################################################################
## Load up third parties
#######################################################################################################################
if (UNDOS_SDK_ONLY)
    add_subdirectory(kernel_userland_shared)
    add_compile_options(-march=i386)
    add_subdirectory(userland/crt)
    add_subdirectory(userland/gui)
    add_subdirectory(sdk)
    return()
endif ()

add_subdirectory(third_party)

#######################################################################################################################
## Add sources
#######################################################################################################################
add_subdirectory(libkcpp)
add_subdirectory(kernel_api)
add_subdirectory(kernel_userland_shared)

if (NOT TESTING_ONLY)
    add_subdirectory(kernel_hal)
    add_subdirectory(kernel)
    add_subdirectory(drivers)
    add_subdirectory(subsystems)

    add_subdirectory(userland)
    add_subdirectory(sdk)

    #######################################################################################################################
    ## Tools that generate the initial image 
    #######################################################################################################################

    add_custom_target(Bin
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/kernel.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hal_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/hal_x86_old.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:boot_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot

            DEPENDS kernel hal_x86_old boot_x86_old
            $<TARGET_FILE:kernel> $<TARGET_FILE:hal_x86_old> $<TARGET_FILE:boot_x86_old>

            BYPRODUCTS ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
    )

    # Loadable subsystems registered through add_subsystem(): staged under
    # System/Subsys and authored into the LocalMachine registry.
    get_property(UNDOS_SUBSYSTEM_TARGETS GLOBAL PROPERTY UNDOS_SUBSYSTEMS)
    set(SUBSYSTEM_STAGE_COMMANDS)
    set(SUBSYSTEM_REGISTRY_SPECS)
    foreach (subsystem ${UNDOS_SUBSYSTEM_TARGETS})
        get_property(subsystem_file GLOBAL PROPERTY UNDOS_SUBSYSTEM_${subsystem}_FILE)
        get_property(subsystem_start GLOBAL PROPERTY UNDOS_SUBSYSTEM_${subsystem}_START)
        list(APPEND SUBSYSTEM_STAGE_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${subsystem}>
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Subsys/${subsystem_file})
        list(APPEND SUBSYSTEM_REGISTRY_SPECS "${subsystem}:${subsystem_start}:/System/Subsys/${subsystem_file}")
    endforeach ()

    add_custom_target(make-initial-disk
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/bin
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Config
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Fonts
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Subsys
            ${SUBSYSTEM_STAGE_COMMANDS}
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_elf_loader> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/ELoader.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bus_isa> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/bus_isa.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:disk_ata> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/disk_ata.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:volmgr> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/volmgr.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fs_fat> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/fs_fat.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfx_vga> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/gfx_vga.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfx_cirrus> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/gfxcirr.elf
            # NOTE: the destination filename must have an 8.3-compatible base
            # (<=8 chars) since drivers/fs_fat/fat_short_name.hpp's short-name
            # lookup has no truncation/tilde fallback for longer names.
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:input_i8042> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/i8042.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:input_gameport> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/gameport.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:serial_uart> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/serial.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:term> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/term.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:progman> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/progman.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:demo> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/demo.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fontloader> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/fontload.elf
            COMMAND ${PYTHON3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/create-font-fixture.py
                    ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Fonts/FTEST.TTF
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/userland/apps/progman/PROGMAN.INI ${CMAKE_SOURCE_DIR}/bin/filesystem/System/PROGMAN.INI
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/userland/apps/init/DISPLAY.CFG ${CMAKE_SOURCE_DIR}/bin/filesystem/System/DISPLAY.CFG
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/init.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:abi_test> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/abitest.elf
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/userland/apps/abitest/abitest.so ${CMAKE_SOURCE_DIR}/bin/filesystem/System/abitest.so
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/userland/apps/abitest/abinext.so ${CMAKE_SOURCE_DIR}/bin/filesystem/System/abinext.so
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/userland/apps/abitest/abimiss.elf ${CMAKE_SOURCE_DIR}/bin/filesystem/System/abimiss.elf
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/userland/apps/init/initlib.so ${CMAKE_SOURCE_DIR}/bin/filesystem/System/initlib.so
            COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/configuration_editor/config.py
                    ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Config/MACHI
                    author-localmachine ${SUBSYSTEM_REGISTRY_SPECS}
            COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/make-initial-disk.py ${PROJECT_SOURCE_DIR}/bin/fat32.img ${PROJECT_SOURCE_DIR}/bin/filesystem

            DEPENDS ${UNDOS_SUBSYSTEM_TARGETS} native_elf_loader bus_isa disk_ata volmgr fs_fat gfx_vga gfx_cirrus input_i8042 input_gameport serial_uart term progman demo fontloader init init_shared abi_test abi_test_shared abi_test_dependent abi_test_missing_note
            $<TARGET_FILE:native_elf_loader>
            $<TARGET_FILE:bus_isa> $<TARGET_FILE:disk_ata> $<TARGET_FILE:volmgr> $<TARGET_FILE:fs_fat> $<TARGET_FILE:gfx_vga> $<TARGET_FILE:gfx_cirrus> $<TARGET_FILE:input_i8042> $<TARGET_FILE:input_gameport>
            $<TARGET_FILE:serial_uart>
            $<TARGET_FILE:term> $<TARGET_FILE:init> ${CMAKE_BINARY_DIR}/userland/apps/init/initlib.so
            $<TARGET_FILE:progman> $<TARGET_FILE:demo>
            $<TARGET_FILE:fontloader>
            ${CMAKE_SOURCE_DIR}/userland/apps/progman/PROGMAN.INI
            ${CMAKE_SOURCE_DIR}/userland/apps/init/DISPLAY.CFG
            ${PROJECT_SOURCE_DIR}/tools/configuration_editor/config.py
            ${PROJECT_SOURCE_DIR}/tools/make-initial-disk.py
            ${PROJECT_SOURCE_DIR}/tools/create-font-fixture.py
            BYPRODUCTS ${PROJECT_SOURCE_DIR}/bin/fat32.img
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Config/MACHI
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/bus_isa.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/disk_ata.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/volmgr.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/fs_fat.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/gfx_vga.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/gfxcirr.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/i8042.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Drivers/serial.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/term.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/progman.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/demo.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/fontload.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/Fonts/FTEST.TTF
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/PROGMAN.INI
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/DISPLAY.CFG
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/init.elf
                ${CMAKE_SOURCE_DIR}/bin/filesystem/System/initlib.so

            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating initial disk image"
    )

    configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/grub.cfg.in
            ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub/grub.cfg
    )

    add_custom_target(BootIso
            COMMAND grub-file --is-x86-multiboot ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
            COMMAND grub-mkrescue
            --directory=${CMAKE_SOURCE_DIR}/bin/tools/grub/i386-pc
            -o ${CMAKE_SOURCE_DIR}/bin/os.iso ${CMAKE_SOURCE_DIR}/bin/isofiles

            DEPENDS Bin make-initial-disk ${PROJECT_SOURCE_DIR}/bin/fat32.img

            COMMENT "Building bootable ISO image"
    )

else ()
    add_subdirectory(hosted_test)
endif ()
