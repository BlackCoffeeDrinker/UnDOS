enable_language(ASM)

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
        src/scheduler/Scheduler.cpp
        src/driver_loader.cpp
        src/driver_loader.hpp
        src/legacy_isa_enum.cpp
        src/legacy_isa_enum.hpp
        src/elf_loader.hpp
        src/elf_loader.cpp
        src/elf_segment_loader.hpp
        src/elf_segment_loader.cpp
        src/process.hpp
        src/process.cpp
        src/syscalls/handler_syscall.cpp
        src/pnp_manager.cpp
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
        src/stdkrn.hpp
        src/module/symbol_entry.cpp
        src/module/symbol_entry.hpp
        src/module/symbol_table.cpp
        src/module/symbol_table.hpp
        src/guard/guard.cpp
        src/atexit.cpp
        src/io.cpp
        src/dispatch.cpp
        src/dispatch.hpp
        src/vfs.cpp
        src/vfs.hpp
        src/user_process.cpp
        src/initial/boot_device_vfs.cpp
        src/initial/boot_device_vfs.hpp
        src/initial/mini_fat16.cpp
        src/initial/mini_fat16.hpp
        src/elf_loader.cpp
        src/elf_loader.hpp
        src/syscalls/DEBUG_Print.cpp
        src/syscalls/CreateProcess.cpp
        src/configuration_manager.cpp
        src/configuration_manager_io.cpp
        src/syscalls/IO.cpp
        src/syscalls/TTY.cpp
        src/syscalls/GFX.cpp
        src/syscalls/User.cpp
        src/user/user.hpp
        src/user/region.hpp
        src/user/msgring.hpp
        src/user/window.cpp
        src/user/msgqueue.cpp
        src/user/paint.cpp
        src/user/nonclient.cpp
        src/user/rawinput.cpp
        src/gfx/gfx_management.cpp
        src/gfx/gfx_software_fallback.cpp
        src/gfx/gfx_dc_table.cpp

        src/input/input_manager.cpp
        src/input/input_manager.hpp

        src/tty/tty.cpp
        src/tty/tty_device.cpp
)

add_dependencies(kernel undos-syscalls-gen)

target_compile_definitions(kernel PRIVATE UNDOS_KERNEL_COMPILE)
target_link_libraries(kernel PRIVATE undos_warnings undos_options)
target_link_libraries(kernel PUBLIC libkcpp KernelHeaders KernelUserlandShared)
target_include_directories(kernel PRIVATE src ${CMAKE_SOURCE_DIR}/drivers/volmgr)

target_compile_options(kernel PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -flto)
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

add_custom_command(TARGET kernel POST_BUILD
        COMMAND readelf -S $<TARGET_FILE:kernel>
        COMMAND readelf -s $<TARGET_FILE:kernel>
        COMMAND objdump -h $<TARGET_FILE:kernel>
)
