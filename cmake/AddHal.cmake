

function(add_hal TARGET_NAME)
    # Remaining arguments are source files
    set(SOURCES ${ARGN})
    #add_executable(${TARGET_NAME} ${SOURCES})
    add_library(${TARGET_NAME} SHARED ${SOURCES})
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${TARGET_NAME}
            PRIVATE
            libkcpp
            KernelHeaders
    )

    target_compile_definitions(${TARGET_NAME}
            PRIVATE
            UNDOS_HAL_COMPILE
    )

    target_compile_options(${TARGET_NAME}
            PRIVATE
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -flto
    )

    target_link_options(${TARGET_NAME}
            PRIVATE
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -flto
            "-T${CMAKE_SOURCE_DIR}/kernel_hal/linker.ld"
            -nostdlib
            -static
            "-Wl,--export-dynamic"
            -Wl,--emit-relocs
            -Wl,--unresolved-symbols=ignore-all
            -Wl,--no-warn-rwx-segments
    )
endfunction()
