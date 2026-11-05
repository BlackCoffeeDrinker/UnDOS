get_filename_component(UNDOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/PreventInSourceBuilds.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CompilerWarnings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/StandardProjectSettings.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(UNDOS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

add_library(undos_warnings INTERFACE)
add_library(undos_options INTERFACE)

undos_set_project_warnings(undos_warnings ${UNDOS_WARNINGS_AS_ERRORS} "" "" "" "")
target_compile_features(undos_options INTERFACE cxx_std_20)

if (NOT TESTING_ONLY)
    set(CMAKE_C_STANDARD_LIBRARIES "-lgcc")
    set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc")
    target_compile_options(undos_options INTERFACE
            -m32 -march=i386 -nostdlib -fno-builtin -fno-rtti -fno-exceptions
            -fno-stack-protector -ffreestanding -Wall -Wextra)
    target_link_options(undos_options INTERFACE
            -nostdlib -nostartfiles -nodefaultlibs -nostdinc -nolibc
            -Wl,--oformat=elf32-i386 -Wl,--nmagic)
endif ()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON3_EXECUTABLE "${Python3_EXECUTABLE}")
set(UNDOS_SYSCALL_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/syscalls")
set(UNDOS_SYSCALL_KERNEL_DIR "${UNDOS_SYSCALL_GENERATED_DIR}/kernel/syscalls")
# The generator rule and the targets exposing its output live in ABI/CMakeLists.txt.
