# USER: windows, menus, message queues, painting and raw input.
add_library(kernel_user OBJECT
        src/syscalls/User.cpp
        src/user/window.cpp
        src/user/menu.cpp
        src/user/msgqueue.cpp
        src/user/paint.cpp
        src/user/nonclient.cpp
        src/user/rawinput.cpp
)
undos_kernel_component(kernel_user USER)
target_link_libraries(kernel_user PRIVATE GFX_Interface)
