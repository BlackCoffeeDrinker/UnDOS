cmake_minimum_required(VERSION 3.27)
project(MenuUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(menu main.c)
target_compile_features(menu PRIVATE c_std_11)
set_target_properties(menu PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(menu PRIVATE UnDOS::GUI)
