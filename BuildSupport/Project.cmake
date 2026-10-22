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
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON3_EXECUTABLE "${Python3_EXECUTABLE}")
set(UNDOS_SYSCALL_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/syscalls")
set(UNDOS_SYSCALL_INCLUDE_DIR "${UNDOS_SYSCALL_GENERATED_DIR}/include")
set(UNDOS_SYSCALL_KERNEL_DIR "${UNDOS_SYSCALL_GENERATED_DIR}/kernel/syscalls")
file(GLOB UNDOS_SYSCALL_INPUTS CONFIGURE_DEPENDS
    "${UNDOS_ROOT}/ABI/IDL/*.txt"
    "${UNDOS_ROOT}/Tools/idlgen/*.py")
list(APPEND UNDOS_SYSCALL_INPUTS "${UNDOS_ROOT}/Tools/create-syscalls.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UNDOS_SYSCALL_INPUTS})
set(UNDOS_SYSCALL_GENERATOR_ARGS
    "${UNDOS_ROOT}/ABI/IDL/syscalls.txt"
    "${UNDOS_SYSCALL_INCLUDE_DIR}/undos/syscalls"
    "${UNDOS_SYSCALL_INCLUDE_DIR}"
    "${UNDOS_SYSCALL_KERNEL_DIR}")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${UNDOS_ROOT}/Tools/create-syscalls.py"
        --list-outputs ${UNDOS_SYSCALL_GENERATOR_ARGS}
    OUTPUT_VARIABLE UNDOS_SYSCALL_OUTPUTS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" UNDOS_SYSCALL_OUTPUTS "${UNDOS_SYSCALL_OUTPUTS}")
set(UNDOS_SYSCALL_PUBLIC_HEADERS ${UNDOS_SYSCALL_OUTPUTS})
list(FILTER UNDOS_SYSCALL_PUBLIC_HEADERS INCLUDE REGEX "/include/undos/syscalls/[^/]+\\.h$")
add_custom_command(OUTPUT ${UNDOS_SYSCALL_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${UNDOS_ROOT}/Tools/create-syscalls.py"
        ${UNDOS_SYSCALL_GENERATOR_ARGS}
    DEPENDS ${UNDOS_SYSCALL_INPUTS}
    COMMENT "Generating the shared syscall ABI"
    VERBATIM)
add_custom_target(undos-syscalls-gen DEPENDS ${UNDOS_SYSCALL_OUTPUTS})
