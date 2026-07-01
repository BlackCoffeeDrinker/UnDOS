if (NOT TESTING_ONLY)
    return()
endif ()

if (NOT TARGET Catch2::Catch2WithMain)
    CPMAddPackage("gh:catchorg/Catch2@3.15.1")
endif ()
enable_testing()

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

add_subdirectory(main)
add_subdirectory(tests)
