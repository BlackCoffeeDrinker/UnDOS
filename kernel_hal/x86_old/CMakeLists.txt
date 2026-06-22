## Include the actual boot loader
add_subdirectory(boot)


set_source_files_properties(
        intr.cpp
        PROPERTIES COMPILE_FLAGS "-mgeneral-regs-only -mno-80387 -mno-mmx -mno-sse -mno-sse2"
)

add_executable(hal_x86_old
        ia32.cpp
        ia32.hpp
        structs.hpp
        intr.hpp
        intr.cpp
        gdt.cpp
        gdt.hpp
        vmm.cpp
        vmm.hpp
        pmm.cpp
        debug_serial.cpp
        pit.cpp
        pit.hpp
        pmm.hpp
)

target_include_directories(hal_x86_old PUBLIC .)
target_include_directories(hal_x86_old PRIVATE include)
target_link_libraries(hal_x86_old PRIVATE libkcpp KernelHeaders)
target_compile_definitions(hal_x86_old PRIVATE UNDOS_HAL_COMPILE)
target_compile_options(hal_x86_old PRIVATE -fvisibility=hidden)
target_link_options(hal_x86_old PRIVATE -fvisibility=hidden)

target_link_options(hal_x86_old PRIVATE
        "-T" "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld"
        "-nostdlib"
        "-static"
        "-Wl,--emit-relocs" # CRITICAL: Allows Stage 1.5 to stitch HAL symbols
        "-Wl,--unresolved-symbols=ignore-all"  # CRITICAL: Allow missing Kernel symbols
        "-Wl,--no-warn-rwx-segments"           # Suppress the security warning
)
