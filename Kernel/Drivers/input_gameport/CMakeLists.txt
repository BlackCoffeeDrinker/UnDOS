
add_driver(input_gameport gameport.cpp)

target_link_libraries(input_gameport PRIVATE KernelUserlandShared input_hid)
