enable_language(ASM)

add_executable(kernel
        src/memchr.cpp
        src/strlen.cpp

        src/Core.hpp
        src/main.cpp

        src/vmm.cpp
        src/vmm.hpp
        src/time.cpp
        src/driver_loader.cpp
        src/driver_loader.hpp
        src/object_manager.cpp
        src/object_manager.hpp
        src/common/DoubleListLink.hpp
        src/common/DoubleList.hpp

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
)

target_compile_definitions(kernel PRIVATE UNDOS_KERNEL_COMPILE)
target_link_libraries(kernel PRIVATE undos_warnings undos_options)
target_link_libraries(kernel PUBLIC libkcpp KernelHeaders)
target_include_directories(kernel PRIVATE src)

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
