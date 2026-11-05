add_library(undosgui STATIC windows.c utf8.c ui.c windows_backend.c)
target_include_directories(undosgui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(undosgui PUBLIC undoscrt KernelUserlandShared)
set_target_properties(undosgui PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
set_source_files_properties(windows_backend.c PROPERTIES COMPILE_OPTIONS "-std=gnu2x")
target_compile_options(undosgui PRIVATE
    -ffreestanding -nostdlib -fno-pic -fno-pie -m32 -march=i386 -Os)
