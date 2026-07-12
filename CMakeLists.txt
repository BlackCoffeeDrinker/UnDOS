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
## Load up third parties
#######################################################################################################################
add_subdirectory(third_party)

#######################################################################################################################
## Add sources
#######################################################################################################################
add_subdirectory(libkcpp)
add_subdirectory(kernel_api)
if (NOT TESTING_ONLY)
    add_subdirectory(kernel_hal)
    add_subdirectory(kernel)
    add_subdirectory(drivers)
else ()
    add_subdirectory(hosted_test)
endif ()



#######################################################################################################################
## Tools that generate the initial image 
#######################################################################################################################
add_custom_target(make-initial-disk
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/bin
        COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/make-initial-disk.py ${PROJECT_SOURCE_DIR}/bin/fat32.img ${PROJECT_SOURCE_DIR}/bin/filesystem
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating initial disk image"
)
