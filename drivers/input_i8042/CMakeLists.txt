
add_driver(input_i8042 i8042.cpp ps2_pump.cpp)

target_link_libraries(input_i8042 PRIVATE KernelUserlandShared input_hid)
