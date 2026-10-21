cmake_minimum_required(VERSION 3.27)
project(Init LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_library(initlib SHARED shared_message.cpp)
target_link_libraries(initlib PRIVATE UnDOS::Shared)

add_executable(init main.cpp)
target_compile_features(init PRIVATE cxx_std_20)
target_link_libraries(init PRIVATE initlib UnDOS::Application)
