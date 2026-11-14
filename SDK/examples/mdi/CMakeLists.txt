cmake_minimum_required(VERSION 3.27)
project(MdiUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(mdi main.c)
target_compile_features(mdi PRIVATE c_std_11)
set_target_properties(mdi PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(mdi PRIVATE UnDOS::GUI)
