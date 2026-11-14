cmake_minimum_required(VERSION 3.27)
project(FilesUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(files main.c)
target_compile_features(files PRIVATE c_std_11)
set_target_properties(files PROPERTIES C_STANDARD_REQUIRED ON)
target_link_libraries(files PRIVATE UnDOS::GUI)
