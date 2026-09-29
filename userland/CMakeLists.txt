# Userland does not use undos_options; without this gcc defaults to i686 and
# emits cmov, which faults (#UD) on the 486 target.
add_compile_options(-march=i386)

add_subdirectory(crt)
add_subdirectory(elf_loader)
add_subdirectory(apps/term)
add_subdirectory(apps/init)
