cmake_minimum_required(VERSION 3.27)
project(Demo LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_executable(demo main.cpp)
target_compile_features(demo PRIVATE cxx_std_20)
target_link_libraries(demo PRIVATE UnDOS::GUI)
