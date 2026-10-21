# SDK consumers configure separately so they cannot inherit kernel build settings.
function(undos_sdk_application target source)
    set(build_dir "${CMAKE_BINARY_DIR}/sdk-apps/${target}/$<CONFIG>")
    set(outputs)
    foreach(file IN LISTS ARGN)
        list(APPEND outputs "${build_dir}/${file}")
    endforeach()
    add_custom_target(${target}
        COMMAND ${CMAKE_COMMAND}
            -S "${PROJECT_SOURCE_DIR}/${source}"
            -B "${build_dir}"
            -G "${CMAKE_GENERATOR}"
            "-DCMAKE_TOOLCHAIN_FILE=${UNDOS_SDK_PREFIX}/share/cmake/UnDOS/UnDOS-toolchain.cmake"
            "-DUnDOS_DIR=${UNDOS_SDK_PREFIX}/share/cmake/UnDOS"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_BUILD_TYPE=$<CONFIG>"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${build_dir}"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_$<UPPER_CASE:$<CONFIG>>=${build_dir}"
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${build_dir}"
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_$<UPPER_CASE:$<CONFIG>>=${build_dir}"
        COMMAND ${CMAKE_COMMAND} --build "${build_dir}" --config "$<CONFIG>"
        BYPRODUCTS ${outputs}
        DEPENDS sdk
        COMMENT "Building ${target} against the staged SDK"
        VERBATIM)
    set(UNDOS_${target}_DIR "${build_dir}" PARENT_SCOPE)
endfunction()

undos_sdk_application(progman userland/apps/progman progman.elf)
undos_sdk_application(term userland/apps/term term.elf)
undos_sdk_application(demo userland/apps/demo demo.elf)
undos_sdk_application(init userland/apps/init init.elf initlib.so)
undos_sdk_application(abi_test userland/apps/abitest abitest.elf abitest.so abinext.so abimiss.elf)
undos_sdk_application(fontloader userland/fontloader fontload.elf)
