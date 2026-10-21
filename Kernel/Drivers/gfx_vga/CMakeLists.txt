
add_driver(gfx_vga vga.cpp)

target_link_libraries(gfx_vga PRIVATE KernelUserlandShared)
