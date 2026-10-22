cmake_minimum_required(VERSION 3.27)
project(GUIUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(gui main.c)
target_link_libraries(gui PRIVATE UnDOS::GUI)
