
# The shared HID class driver is not a loadable module of its own: drivers
# are standalone ELF images linked with --unresolved-symbols=ignore-all, so
# a separate library would never be resolved at load time. It is published
# as an INTERFACE target instead, which compiles hid_parser.cpp directly
# into every transport driver that links it (input_i8042, input_gameport,
# and a future USB host controller).
add_library(input_hid INTERFACE)

target_sources(input_hid INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hid_parser.cpp)
target_include_directories(input_hid INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
