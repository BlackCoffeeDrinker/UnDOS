cmake_minimum_required(VERSION 3.27)
project(DialogUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(dialog main.c)
target_compile_features(dialog PRIVATE c_std_11)
set_target_properties(dialog PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(dialog PRIVATE UnDOS::GUI)
