# Userland does not use undos_options; without this gcc defaults to i686 and
# emits cmov, which faults (#UD) on the 486 target.
add_compile_options(-march=i386)

add_subdirectory(crt)
add_subdirectory(elf_loader)
add_subdirectory(gui)
add_subdirectory(apps/term)
add_subdirectory(apps/init)
add_subdirectory(apps/progman)
add_subdirectory(apps/demo)
add_subdirectory(fontloader)
add_subdirectory(apps/abitest)

# All in-tree application images migrate together; no legacy ABI exemption.
foreach(application term init progman demo fontloader native_elf_loader)
    target_sources(${application} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/abi_note.S)
    target_link_options(${application} PRIVATE -Wl,--export-dynamic -Wl,--hash-style=sysv -Wl,--no-dynamic-linker)
    set_property(TARGET ${application} APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/term/linker.ld
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/init/linker.ld)
endforeach()
target_sources(init_shared_object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/abi_note.S)
