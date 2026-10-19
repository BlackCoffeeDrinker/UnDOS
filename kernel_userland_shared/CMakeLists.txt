

add_library(KernelUserlandShared INTERFACE)
target_include_directories(KernelUserlandShared INTERFACE include)
add_dependencies(KernelUserlandShared undos-syscalls-gen)

add_library(undos_font_resource STATIC src/font_resource.cpp)
target_include_directories(undos_font_resource PUBLIC include)
target_link_libraries(undos_font_resource PRIVATE undos_warnings)
if (NOT TESTING_ONLY)
    target_link_libraries(undos_font_resource PRIVATE undos_options)
endif ()
