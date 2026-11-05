cmake_minimum_required(VERSION 3.27)
project(GUIUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(gui main.c)
target_compile_features(gui PRIVATE c_std_11)
set_target_properties(gui PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(gui PRIVATE UnDOS::GUI)
