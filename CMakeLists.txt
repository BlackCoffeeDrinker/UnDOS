cmake_minimum_required(VERSION 3.27)

include(cmake/PreventInSourceBuilds.cmake)

SET(CMAKE_CXX_FLAGS "-Wall -Wextra -m32 -ffreestanding -fno-rtti -fno-exceptions -nostdlib -fno-stack-protector -fno-use-cxa-atexit -fno-builtin")
SET(CMAKE_C_FLAGS "-Wall -Wextra -m32 -ffreestanding           -fno-exceptions -nostdlib -fno-stack-protector")
##
# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(UnDOS
        VERSION 0.0.1
)

#######################################################################################################################
## Global Project targeting
#######################################################################################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)


##
# 1. Global Toolchain/Compiler Sanity Flags for Kernel Space
add_compile_options(
        -nostdlib
        -fno-builtin
        -fno-rtti
        -fno-exceptions
        -ffreestanding
        -Wall
        -Wextra
)
set(tools /usr/local/cross)
add_link_options(-nostdlib -nostartfiles -nodefaultlibs -nostdinc -nolibc -Wl,--oformat=elf32-i386 -Wl,--nmagic -L ${tools}/lib/gcc/i686-elf/14.1.0/)
# Don't link with anything by default unless we say so
set(CMAKE_C_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C Libraries")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C++ Libraries")

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

#######################################################################################################################
## Load up third parties
#######################################################################################################################
add_subdirectory(third_party)

#######################################################################################################################
## Add sources
#######################################################################################################################
add_subdirectory(libkcpp)
add_subdirectory(kernel_api)
add_subdirectory(kernel_hal)
add_subdirectory(kernel)

