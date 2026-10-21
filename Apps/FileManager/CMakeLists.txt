cmake_minimum_required(VERSION 3.27)
project(FileManager LANGUAGES CXX)
find_package(UnDOS CONFIG REQUIRED)

add_executable(winfile main.cpp)
target_compile_features(winfile PRIVATE cxx_std_20)
target_link_libraries(winfile PRIVATE UnDOS::GUI)

option(FILEMANAGER_BUILD_PROBE "Build the destructive-on-test-folder filesystem conformance probe" OFF)
if(FILEMANAGER_BUILD_PROBE)
    add_executable(fmprobe tests/runtime_probe.cpp)
    target_compile_features(fmprobe PRIVATE cxx_std_20)
    target_link_libraries(fmprobe PRIVATE UnDOS::Application)
endif()
