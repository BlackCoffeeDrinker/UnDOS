# Input: kernel input manager (event ring, \Device\Input stream).
add_library(kernel_input OBJECT
        src/input/input_manager.cpp
)
undos_kernel_component(kernel_input Input)
