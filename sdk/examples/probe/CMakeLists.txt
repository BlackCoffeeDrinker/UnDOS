cmake_minimum_required(VERSION 3.27)
project(SDKProbe LANGUAGES C CXX)

find_package(UnDOS CONFIG REQUIRED)
add_library(sdkmsg SHARED library.cpp)
target_link_libraries(sdkmsg PRIVATE UnDOS::Shared)
add_executable(sdkcheck main.cpp query.c libc.c)
target_link_libraries(sdkcheck PRIVATE sdkmsg UnDOS::FontResource UnDOS::Application)
