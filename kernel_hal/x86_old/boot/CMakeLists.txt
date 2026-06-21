
add_executable(boot_x86_old
        entry.S
        multiboot.hpp
        trampoline.cpp
)

target_link_libraries(boot_x86_old PRIVATE libkcpp KernelHeaders)

target_link_options(boot_x86_old PRIVATE
        "-T" "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld"
        "-nostdlib"
        "-static"
        "-Wl,--no-warn-rwx-segments"           # Suppress the security warning
)

add_custom_target(BootIso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/grub
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/kernel.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hal_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/hal_x86_old.elf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:boot_x86_old> ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
        COMMAND grub-file --is-x86-multiboot ${CMAKE_SOURCE_DIR}/bin/isofiles/boot/undos-boot
        COMMAND grub-mkrescue
        --directory=${CMAKE_SOURCE_DIR}/bin/tools/grub/i386-pc
        -o ${CMAKE_SOURCE_DIR}/bin/os.iso ${CMAKE_SOURCE_DIR}/bin/isofiles

        DEPENDS kernel hal_x86_old boot_x86_old $<TARGET_FILE:kernel> $<TARGET_FILE:hal_x86_old> $<TARGET_FILE:boot_x86_old>
        COMMENT "Building bootable ISO image"
)
