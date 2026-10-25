cmake_minimum_required(VERSION 3.27)
project(FontLoader LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_library(fontdecode STATIC decoder.cpp)
target_compile_features(fontdecode PUBLIC cxx_std_20)
target_link_libraries(fontdecode PUBLIC UnDOS::KStd UnDOS::FontResource)
target_include_directories(fontdecode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(fontloader main.cpp)
target_link_libraries(fontloader PRIVATE fontdecode UnDOS::GUI)
set_target_properties(fontloader PROPERTIES OUTPUT_NAME fontload)
