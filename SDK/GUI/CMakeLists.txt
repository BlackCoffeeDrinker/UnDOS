add_library(undosgui STATIC gui.cpp windows.c)
target_include_directories(undosgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(undosgui PUBLIC undoscrt KernelUserlandShared)
set_target_properties(undosgui PROPERTIES POSITION_INDEPENDENT_CODE OFF)
target_compile_options(undosgui PRIVATE
    -ffreestanding -nostdlib -fno-pic -fno-pie -m32 -march=i386
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
