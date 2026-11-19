# Profiler: optional -finstrument-functions profiler (UNDOS_PROFILE). With the
# option OFF only Profiler_Interface exists; its header provides inline stubs.
if (UNDOS_PROFILE)
    add_library(kernel_profiler OBJECT
            src/profiler/profiler.cpp
    )
    undos_kernel_component(kernel_profiler Profiler)
    target_link_libraries(kernel_profiler PRIVATE Executive_Interface)
    target_compile_options(kernel_profiler PRIVATE -fno-instrument-functions)
else ()
    add_library(Profiler_Interface INTERFACE)
    target_include_directories(Profiler_Interface INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif ()
