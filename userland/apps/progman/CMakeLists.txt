cmake_minimum_required(VERSION 3.27)
project(ProgramManager LANGUAGES CXX)

find_package(UnDOS CONFIG REQUIRED)

add_executable(progman main.cpp)
target_compile_features(progman PRIVATE cxx_std_20)
target_link_libraries(progman PRIVATE UnDOS::GUI)
