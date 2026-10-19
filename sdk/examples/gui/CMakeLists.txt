cmake_minimum_required(VERSION 3.27)
project(GUIUnDOS LANGUAGES CXX)

find_package(UnDOS CONFIG REQUIRED)
add_executable(gui main.cpp)
target_link_libraries(gui PRIVATE UnDOS::GUI)
