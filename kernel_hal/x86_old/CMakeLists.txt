## Include the actual boot loader
add_subdirectory(boot)

set_source_files_properties(
        intr.cpp
        PROPERTIES COMPILE_FLAGS "-mgeneral-regs-only -mno-80387 -mno-mmx -mno-sse -mno-sse2"
)

add_hal(hal_x86_old
        ia32.cpp
        ia32.hpp
        context.cpp
        structs.hpp
        intr.hpp
        intr.cpp
        gdt.cpp
        gdt.hpp
        tss.hpp
        vmm.cpp
        vmm.hpp
        pmm.cpp
        debug_serial.cpp
        pit.cpp
        pit.hpp
        pmm.hpp
        atomics.cpp
        atomics.hpp
        cpuid.cpp
        cpuid.hpp
        v86.cpp
        v86.hpp
        bootdev.cpp
)

