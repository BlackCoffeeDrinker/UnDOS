enable_language(ASM)

# Compile settings shared by the kernel and its object libraries, so that
# objects compiled outside the kernel target are built exactly as before.
function(undos_kernel_compile_settings target)
    target_compile_definitions(${target} PRIVATE UNDOS_KERNEL_COMPILE)
    target_link_libraries(${target} PRIVATE undos_warnings undos_options)
    target_link_libraries(${target} PUBLIC libkcpp KernelHeaders KernelUserlandShared KernelSyscallDispatch)
    target_link_libraries(${target} PRIVATE undos_font_resource)
    target_include_directories(${target} PRIVATE src ${UNDOS_ROOT}/Kernel/Drivers/volmgr)
    target_include_directories(${target} PRIVATE src/syscalls)

    target_compile_options(${target} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -flto -fno-use-cxa-atexit)

    if (UNDOS_PROFILE)
        target_compile_definitions(${target} PRIVATE UNDOS_PROFILE=1)
        target_compile_options(${target} PRIVATE -finstrument-functions
                # percpu.hpp / KPtr.hpp: header inlines reached from the hooks
                "-finstrument-functions-exclude-file-list=profiler/,src/strlen.cpp,src/memchr.cpp,src/crt_init.cpp,src/atexit.cpp,src/guard/,src/scheduler/SchedulerSwitch.cpp,src/scheduler/CpuContext.cpp,src/percpu.cpp,src/percpu.hpp,kernel/fwd/KPtr.hpp"
                # Name substrings: blocked-time noise and trivial helpers that only inflate the profile
                "-finstrument-functions-exclude-function-list=schedule_next,Address<,span<,operator new,char_traits")
    endif ()
endfunction()

# Kernel components (Kernel/Components): object libraries compiled with the
# kernel settings. Each component publishes its kernel interface in
# include/ ("kernel/<name>/...") through an INTERFACE target <name>_Interface;
# src/ stays private. Components use each other only via <name>_Interface.
function(undos_kernel_component target name)
    add_library(${name}_Interface INTERFACE)
    target_include_directories(${name}_Interface INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${name}_Interface INTERFACE libkcpp KernelHeaders KernelUserlandShared KernelSyscallDispatch)

    undos_kernel_compile_settings(${target})
    target_link_libraries(${target} PUBLIC ${name}_Interface)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # CMake would compile a library with -fPIC; match the kernel executable's -fPIE.
    get_target_property(_pic ${target} POSITION_INDEPENDENT_CODE)
    if (_pic)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_compile_options(${target} PRIVATE ${CMAKE_CXX_COMPILE_OPTIONS_PIE})
    endif ()
endfunction()

add_executable(kernel
        src/memchr.cpp
        src/strlen.cpp
        src/main.cpp
        "${UNDOS_SYSCALL_KERNEL_DIR}/handler_syscall.cpp"
        src/guard/guard.cpp
        src/atexit.cpp
        src/crt_init.cpp
        src/crt_init.hpp
        src/syscalls/ABI.cpp
        src/konsole.cpp
        src/kerror.cpp
        src/syscalls/KErrorToSysReturnCode.cpp
        src/syscalls/KErrorToSysReturnCode.hpp
        src/CommandLineParser.hpp
)

undos_kernel_compile_settings(kernel)
target_link_libraries(kernel PRIVATE kernel_executive kernel_io kernel_user kernel_gfx kernel_initial kernel_input kernel_tty kernel_pnp kernel_loader Profiler_Interface)
if (UNDOS_PROFILE)
    target_link_libraries(kernel PRIVATE kernel_profiler)
endif ()

target_link_options(kernel PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -flto)

# Only UNDOS_KERNEL_PUBLIC_V1API symbols (plus the C++ ABI allowlist) may be
# exported by the kernel and its components; everything else is private.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET kernel POST_BUILD
        # Replaceable operator new/delete ignore -fvisibility=hidden.
        COMMAND "${CMAKE_OBJCOPY}" "--localize-symbols=${CMAKE_CURRENT_SOURCE_DIR}/kernel_localized_symbols.txt"
                "$<TARGET_FILE:kernel>"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.py"
                "${CMAKE_READELF}" "$<TARGET_FILE:kernel>"
                "${CMAKE_CURRENT_SOURCE_DIR}/kernel_exports_allowlist.txt"
                "${UNDOS_ROOT}/Kernel/API" "${UNDOS_ROOT}/Kernel/KStd/include" "${UNDOS_ROOT}/Kernel/Source/src" "${UNDOS_ROOT}/Kernel/Components"
        COMMENT "Checking kernel exports are UNDOS_KERNEL_PUBLIC_V1API"
        VERBATIM)

target_link_options(kernel PRIVATE
        "-T" "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld"
        #"-rdynamic" 
        "-Wl,--export-dynamic"
        "-nostdlib"
        "-static"
        "-Wl,--emit-relocs"                    # CRITICAL: Keeps relocation sections intact for Stage 1.5
        "-Wl,--unresolved-symbols=ignore-all"  # CRITICAL: Allow missing HAL symbols
        "-Wl,--no-warn-rwx-segments"           # Suppress the security warning
)
