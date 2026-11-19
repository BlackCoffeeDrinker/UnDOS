# TTY: virtual terminals, TTY devices and their IO stream adapters.
add_library(kernel_tty OBJECT
        src/tty/tty.cpp
        src/tty/tty_device.cpp
        src/tty/tty_stream.cpp
        src/syscalls/TTY.cpp
        src/syscalls/DEBUG_Print.cpp
)
undos_kernel_component(kernel_tty TTY)
target_link_libraries(kernel_tty PRIVATE IO_Interface)
