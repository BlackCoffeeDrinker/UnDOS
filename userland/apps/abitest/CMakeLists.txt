cmake_minimum_required(VERSION 3.27)
project(ApplicationABIProbe LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_library(abi_test_shared SHARED shared.cpp)
target_link_libraries(abi_test_shared PRIVATE UnDOS::Shared)
set_target_properties(abi_test_shared PROPERTIES OUTPUT_NAME abitest)

add_library(abi_test_dependent SHARED dependent.cpp)
target_link_libraries(abi_test_dependent PRIVATE abi_test_shared UnDOS::Shared)
set_target_properties(abi_test_dependent PROPERTIES OUTPUT_NAME abinext)

add_executable(abi_test main.cpp)
target_compile_features(abi_test PRIVATE cxx_std_20)
target_link_libraries(abi_test PRIVATE abi_test_shared abi_test_dependent UnDOS::Application)
set_target_properties(abi_test PROPERTIES OUTPUT_NAME abitest)

# Keep the deliberately invalid fixture: applications without the note must reject.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf
    COMMAND ${CMAKE_OBJCOPY} --remove-section .note.undos $<TARGET_FILE:abi_test>
            ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf
    DEPENDS abi_test VERBATIM)
add_custom_target(abi_test_missing_note ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf)
