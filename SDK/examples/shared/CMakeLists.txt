cmake_minimum_required(VERSION 3.27)
project(SharedUnDOS LANGUAGES CXX)

find_package(UnDOS CONFIG REQUIRED)
add_library(message SHARED message.cpp)
target_link_libraries(message PRIVATE UnDOS::Shared)
set_target_properties(message PROPERTIES OUTPUT_NAME message)

add_executable(shared main.cpp)
target_link_libraries(shared PRIVATE message UnDOS::Application)
