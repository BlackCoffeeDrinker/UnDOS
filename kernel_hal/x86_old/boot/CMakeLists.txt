
add_executable(boot_x86_old
        entry.S
        trampoline.S
        multiboot.hpp
        trampoline.cpp
)

target_link_libraries(boot_x86_old PRIVATE libkcpp KernelHeaders undos_warnings undos_options)

target_compile_options(boot_x86_old PRIVATE
        -m32
        -march=i386
        -nostdlib
        -fno-builtin
        -fno-rtti
        -fno-exceptions
        -fno-stack-protector
        -ffreestanding
        -Wall
        -Wextra
)

target_link_options(boot_x86_old PRIVATE
        "-T" "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld"
        "-L ${tools}/lib/gcc/i686-elf/14.1.0/"
        "-nostdlib"
        "-nostartfiles"
        "-nodefaultlibs"
        "-nostdinc"
        "-nolibc"
        "-static"
        "-Wl,--no-warn-rwx-segments"           # Suppress the security warning
)
