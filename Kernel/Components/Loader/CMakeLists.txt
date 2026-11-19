# Loader: ELF images, boot/demand driver loading, subsystems, boot modules
# and the legacy ISA enumerator.
add_library(kernel_loader OBJECT
        src/driver_loader.cpp
        src/subsystem_loader.cpp
        src/legacy_isa_enum.cpp
        src/elf_loader.cpp
        src/elf_segment_loader.cpp
        src/module/module_registry.cpp
)
undos_kernel_component(kernel_loader Loader)
target_link_libraries(kernel_loader PRIVATE Executive_Interface PnP_Interface)
