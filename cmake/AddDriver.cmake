

function(add_driver TARGET_NAME)
    # Remaining arguments are source files
    set(SOURCES ${ARGN})
    add_executable(${TARGET_NAME} ${SOURCES})
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${TARGET_NAME}
            PRIVATE
            libkcpp
            KernelHeaders
            undos_options undos_warnings
    )

    target_compile_definitions(${TARGET_NAME}
            PRIVATE
            UNDOS_DRIVER_COMPILE
    )

    target_compile_options(${TARGET_NAME}
            PRIVATE
            -fvisibility=hidden
    )

    target_link_options(${TARGET_NAME}
            PRIVATE
            -fvisibility=hidden
            "-T${CMAKE_SOURCE_DIR}/drivers/driver.ld"
            -nostdlib
            -static
            -Wl,--emit-relocs
            -Wl,--unresolved-symbols=ignore-all
            -Wl,--no-warn-rwx-segments
    )
endfunction()
