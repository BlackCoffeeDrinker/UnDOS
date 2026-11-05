# Syscall ABI generated from the IDL. Configure re-runs when an input changes.
set(_syscall_include_dir "${UNDOS_SYSCALL_GENERATED_DIR}/include")
file(GLOB _syscall_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/IDL/*.txt"
        "${UNDOS_ROOT}/Tools/idlgen/*.py")
list(APPEND _syscall_inputs "${UNDOS_ROOT}/Tools/create-syscalls.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_syscall_inputs})

set(_syscall_generator_args
        "${CMAKE_CURRENT_SOURCE_DIR}/IDL/syscalls.txt"
        "${_syscall_include_dir}/undos/syscalls"
        "${_syscall_include_dir}"
        "${UNDOS_SYSCALL_KERNEL_DIR}")
execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${UNDOS_ROOT}/Tools/create-syscalls.py"
        --list-outputs ${_syscall_generator_args}
        OUTPUT_VARIABLE _syscall_outputs
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" _syscall_outputs "${_syscall_outputs}")

add_custom_command(OUTPUT ${_syscall_outputs}
        COMMAND "${Python3_EXECUTABLE}" "${UNDOS_ROOT}/Tools/create-syscalls.py"
        ${_syscall_generator_args}
        DEPENDS ${_syscall_inputs}
        COMMENT "Generating the shared syscall ABI"
        VERBATIM)
add_custom_target(undos-syscalls-gen DEPENDS ${_syscall_outputs})

# Userland-visible syscall headers; the HEADERS file set is what the SDK installs.
set(_syscall_public_headers ${_syscall_outputs})
list(FILTER _syscall_public_headers INCLUDE REGEX "/include/(undos/syscalls/[^/]+\\.h|userspace\\.h)$")

add_library(KernelUserlandShared INTERFACE)
target_sources(KernelUserlandShared INTERFACE FILE_SET HEADERS
    BASE_DIRS "${_syscall_include_dir}" FILES ${_syscall_public_headers})
target_include_directories(KernelUserlandShared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_dependencies(KernelUserlandShared undos-syscalls-gen)

# Kernel-side dispatch tables generated from the same IDL.
add_library(KernelSyscallDispatch INTERFACE)
target_include_directories(KernelSyscallDispatch INTERFACE
    "${UNDOS_SYSCALL_GENERATED_DIR}/kernel" "${UNDOS_SYSCALL_KERNEL_DIR}")
target_link_libraries(KernelSyscallDispatch INTERFACE KernelUserlandShared)

# Font resource wire format, shared by kernel and userland. An object library so
# each consumer's configuration compiles it with its own flags.
add_library(undos_font_resource OBJECT src/font_resource.cpp)
target_include_directories(undos_font_resource PUBLIC include)
target_link_libraries(undos_font_resource PRIVATE undos_warnings)
if (NOT TESTING_ONLY)
    target_link_libraries(undos_font_resource PRIVATE undos_options)
endif ()
