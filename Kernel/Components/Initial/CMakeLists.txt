# Initial: boot-time root volume (boot-device FAT16 view, root volume selection).
add_library(kernel_initial OBJECT
        src/initial/boot_device_vfs.cpp
        src/initial/root_volume_selector.cpp
        src/initial/mini_fat16.cpp
)
undos_kernel_component(kernel_initial Initial)
