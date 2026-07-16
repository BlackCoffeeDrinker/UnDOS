cmake_minimum_required(VERSION 3.27)

include(cmake/PreventInSourceBuilds.cmake)

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

include(cmake/CPM.cmake)
include(cmake/StandardProjectSettings.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/AddDriver.cmake)
include(cmake/AddHal.cmake)

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
        COMMENT "Generating syscalls from tools/syscalls.txt"
)

#######################################################################################################################
## Load up third parties
#######################################################################################################################
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

    add_subdirectory(userland)

    #######################################################################################################################
    ## Tools that generate the initial image 
    #######################################################################################################################

    add_custom_target(Bin
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bus_isa> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/bus_isa.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fs_fat> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/fs_fat.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:disk_ata> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/disk_ata.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:volmgr> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/volmgr.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/kernel.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hal_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/hal_x86_old.elf
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:boot_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot

            DEPENDS kernel hal_x86_old boot_x86_old bus_isa disk_ata volmgr fs_fat
            $<TARGET_FILE:kernel> $<TARGET_FILE:hal_x86_old> $<TARGET_FILE:boot_x86_old>
            $<TARGET_FILE:bus_isa> $<TARGET_FILE:disk_ata> $<TARGET_FILE:volmgr> $<TARGET_FILE:fs_fat>

            BYPRODUCTS ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
    )

    add_custom_target(make-initial-disk
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/bin
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_elf_loader> ${CMAKE_SOURCE_DIR}/bin/filesystem/System/ELoader.elf
            COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/make-initial-disk.py ${PROJECT_SOURCE_DIR}/bin/fat32.img ${PROJECT_SOURCE_DIR}/bin/filesystem

            DEPENDS native_elf_loader $<TARGET_FILE:native_elf_loader>
            BYPRODUCTS ${PROJECT_SOURCE_DIR}/bin/fat32.img

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
