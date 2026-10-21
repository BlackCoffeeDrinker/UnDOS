# Userland does not use undos_options; without this gcc defaults to i686 and
# emits cmov, which faults (#UD) on the 486 target.
add_compile_options(-march=i386)

add_subdirectory(crt)
add_subdirectory(elf_loader)
add_subdirectory(gui)

# The loader is a bootstrap image, not an application CRT consumer.
target_sources(native_elf_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/abi_note.S)
target_link_options(native_elf_loader PRIVATE
    -Wl,--export-dynamic -Wl,--hash-style=sysv -Wl,--no-dynamic-linker)
