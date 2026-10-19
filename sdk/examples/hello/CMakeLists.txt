cmake_minimum_required(VERSION 3.27)
project(HelloUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(hello main.c)
target_link_libraries(hello PRIVATE UnDOS::Application)
