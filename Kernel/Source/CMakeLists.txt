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
                "-finstrument-functions-exclude-file-list=src/profiler/,src/strlen.cpp,src/memchr.cpp,src/crt_init.cpp,src/atexit.cpp,src/guard/,src/scheduler/SchedulerSwitch.cpp,src/scheduler/CpuContext.cpp,src/percpu.cpp,src/percpu.hpp,kernel/fwd/KPtr.hpp"
                # Name substrings: blocked-time noise and trivial helpers that only inflate the profile
                "-finstrument-functions-exclude-function-list=schedule_next,Address<,span<,operator new,char_traits")
    endif ()
endfunction()

# Kernel components (Kernel/Components): object libraries compiled with the
# kernel settings. Their src/ directory is the public include root, so
# the core includes "gfx/..." and "user/..." headers.
function(undos_kernel_component target)
    undos_kernel_compile_settings(${target})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
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

        src/vmm.cpp
        src/vmm.hpp
        src/time.cpp
        src/wait.cpp
        src/wait.hpp
        src/percpu.cpp
        src/smp.cpp
        src/smp.hpp

        src/scheduler/ReadyQueue.hpp
        src/scheduler/CpuContext.hpp
        src/scheduler/CpuContext.cpp
        src/scheduler/Scheduler.hpp
        src/scheduler/SchedulerSwitch.cpp
        src/scheduler/Scheduler.cpp
        src/driver_loader.cpp
        src/driver_loader.hpp
        src/subsystem_loader.cpp
        src/subsystem_loader.hpp
        src/legacy_isa_enum.cpp
        src/legacy_isa_enum.hpp
        src/elf_loader.hpp
        src/elf_loader.cpp
        src/elf_segment_loader.hpp
        src/elf_segment_loader.cpp
        src/process.hpp
        src/process.cpp
        src/process_notify.hpp
        src/process_notify.cpp
        src/task_stats.hpp
        src/task_stats.cpp
        "${UNDOS_SYSCALL_KERNEL_DIR}/handler_syscall.cpp"
        src/pnp_manager.cpp
        src/pnp_device_state.cpp
        src/dma_manager.cpp
        src/resource_manager.cpp
        src/resource_manager.hpp
        src/object_manager.cpp
        src/object_manager.hpp

        src/memory/Layout.hpp
        src/memory/Layout.cpp
        src/memory/Storage.hpp
        src/memory/Storage.cpp
        src/memory/Slab.hpp
        src/memory/Slab.cpp
        src/memory/Allocator.hpp
        src/memory/Allocator.cpp
        src/memory/Cache.hpp
        src/memory/Cache.cpp
        src/module/module_registry.cpp
        src/module/module_registry.hpp
        src/guard/guard.cpp
        src/atexit.cpp
        src/crt_init.cpp
        src/crt_init.hpp
        src/io.cpp
        src/dispatch.cpp
        src/dispatch.hpp
        src/vfs.cpp
        src/vfs.hpp
        src/user_process.cpp
        src/initial/boot_device_vfs.cpp
        src/initial/boot_device_vfs.hpp
        src/initial/root_volume_selector.cpp
        src/initial/root_volume_selector.hpp
        src/initial/mini_fat16.cpp
        src/initial/mini_fat16.hpp
        src/elf_loader.cpp
        src/elf_loader.hpp
        src/syscalls/DEBUG_Print.cpp
        src/syscalls/CreateProcess.cpp
        src/configuration_manager.cpp
        src/configuration_manager_io.cpp
        src/syscalls/IO.cpp
        src/syscalls/GetStdHandle.cpp
        src/syscalls/ABI.cpp
        src/syscalls/GetStdHandle.cpp
        src/syscalls/TTY.cpp
        src/input/input_manager.cpp
        src/input/input_manager.hpp

        src/tty/tty.cpp
        src/tty/tty_device.cpp
        src/konsole.cpp
        src/event_broadcaster.cpp
        src/event_broadcaster.hpp
        src/kerror.cpp
        src/syscalls/KErrorToSysReturnCode.cpp
        src/syscalls/KErrorToSysReturnCode.hpp
        src/CommandLineParser.hpp
)

undos_kernel_compile_settings(kernel)
target_link_libraries(kernel PRIVATE kernel_user kernel_gfx)

if (UNDOS_PROFILE)
    target_sources(kernel PRIVATE src/profiler/profiler.cpp)
    set_source_files_properties(src/profiler/profiler.cpp PROPERTIES COMPILE_OPTIONS -fno-instrument-functions)
endif ()

target_link_options(kernel PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -flto)

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
