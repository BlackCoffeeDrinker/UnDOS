cmake_minimum_required(VERSION 3.27)
project(UnDOSHosted VERSION 0.0.1 LANGUAGES C CXX)
set(TESTING_ONLY ON)
include(../../BuildSupport/Project.cmake)
include(../../BuildSupport/CPM.cmake)
add_subdirectory(../../SDK/KStd KStd)
add_subdirectory(../../Kernel/API API)
add_subdirectory(../../ABI ABI)

if (NOT TARGET Catch2::Catch2WithMain)
    CPMAddPackage("gh:catchorg/Catch2@3.15.1")
endif ()
enable_testing()

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

add_subdirectory(main)
add_subdirectory(tests)
