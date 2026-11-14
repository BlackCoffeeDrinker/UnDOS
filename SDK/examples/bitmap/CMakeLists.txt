cmake_minimum_required(VERSION 3.27)
project(BitmapUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(bitmap main.c)
target_compile_features(bitmap PRIVATE c_std_11)
set_target_properties(bitmap PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(bitmap PRIVATE UnDOS::GUI)
