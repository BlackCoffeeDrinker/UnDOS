
add_driver(input_i8042 i8042.cpp)

target_link_libraries(input_i8042 PRIVATE KernelUserlandShared)
