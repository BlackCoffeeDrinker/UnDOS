cmake_minimum_required(VERSION 3.27)
project(TaskManager LANGUAGES CXX)

find_package(UnDOS CONFIG REQUIRED)

add_executable(taskmgr main.cpp)
target_compile_features(taskmgr PRIVATE cxx_std_20)
target_compile_options(taskmgr PRIVATE -Os)
target_link_libraries(taskmgr PRIVATE UnDOS::GUI)

add_executable(taskprobe probe.cpp)
target_compile_features(taskprobe PRIVATE cxx_std_20)
target_compile_options(taskprobe PRIVATE -Os)
target_link_libraries(taskprobe PRIVATE UnDOS::Application)
