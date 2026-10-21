cmake_minimum_required(VERSION 3.27)

set(UNDOS_COMPONENT Kernel CACHE STRING "Component opened by the IDE workspace")
set_property(CACHE UNDOS_COMPONENT PROPERTY STRINGS Kernel SDK CRT Hosted)
if (NOT UNDOS_COMPONENT MATCHES "^(Kernel|SDK|CRT|Hosted)$")
    message(FATAL_ERROR "UNDOS_COMPONENT must be Kernel, SDK, CRT, or Hosted")
endif()

if (DEFINED UNDOS_CONFIGURED_COMPONENT AND
    NOT UNDOS_CONFIGURED_COMPONENT STREQUAL UNDOS_COMPONENT)
    message(FATAL_ERROR "Use a separate build directory for each UNDOS_COMPONENT")
endif()
set(UNDOS_CONFIGURED_COMPONENT "${UNDOS_COMPONENT}" CACHE INTERNAL "Configured workspace component")

if (NOT UNDOS_COMPONENT STREQUAL "Hosted" AND NOT CMAKE_TOOLCHAIN_FILE)
    if (UNDOS_COMPONENT STREQUAL "Kernel")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/BuildSupport/i686-toolchain.cmake"
            CACHE FILEPATH "UnDOS component toolchain")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/SDK/UnDOS-toolchain.cmake"
            CACHE FILEPATH "UnDOS component toolchain")
    endif()
endif()

project(UnDOSWorkspace VERSION 0.0.1 LANGUAGES NONE)

if (UNDOS_COMPONENT STREQUAL "Hosted")
    enable_testing()
    add_subdirectory(Tests/Hosted)
else()
    add_subdirectory("${UNDOS_COMPONENT}")
endif()
