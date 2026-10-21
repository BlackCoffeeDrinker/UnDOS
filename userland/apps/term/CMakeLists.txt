cmake_minimum_required(VERSION 3.27)
project(Terminal LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_executable(term main.cpp)
target_compile_features(term PRIVATE cxx_std_20)
target_link_libraries(term PRIVATE UnDOS::GUI)
