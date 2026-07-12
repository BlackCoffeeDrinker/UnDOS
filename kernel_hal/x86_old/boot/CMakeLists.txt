
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

add_custom_target(Bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bus_isa> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/bus_isa.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fs_fat> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/fs_fat.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:disk_ata> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/disk_ata.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:volmgr> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/volmgr.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/kernel.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hal_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/hal_x86_old.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:boot_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot

        DEPENDS kernel hal_x86_old boot_x86_old bus_isa disk_ata volmgr fs_fat 
        $<TARGET_FILE:kernel> $<TARGET_FILE:hal_x86_old> $<TARGET_FILE:boot_x86_old> $<TARGET_FILE:bus_isa> $<TARGET_FILE:disk_ata> $<TARGET_FILE:volmgr>
        $<TARGET_FILE:fs_fat>

        BYPRODUCTS ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/grub.cfg.in
        ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub/grub.cfg
)

add_custom_target(BootIso
        COMMAND grub-file --is-x86-multiboot ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
        COMMAND grub-mkrescue
        --directory=${CMAKE_SOURCE_DIR}/bin/tools/grub/i386-pc
        -o ${CMAKE_SOURCE_DIR}/bin/os.iso ${CMAKE_SOURCE_DIR}/bin/isofiles

        DEPENDS Bin

        COMMENT "Building bootable ISO image"
)

