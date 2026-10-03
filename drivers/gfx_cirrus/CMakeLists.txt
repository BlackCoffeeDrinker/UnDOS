
add_driver(gfx_cirrus cirrus.cpp)

target_link_libraries(gfx_cirrus PRIVATE KernelUserlandShared)
