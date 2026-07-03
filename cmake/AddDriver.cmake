

function(add_driver TARGET_NAME)
    # Remaining arguments are source files
    set(SOURCES ${ARGN})

    # Generate metadata file with driver name
    # We use a non-allocatable section (.driver_name) to store the name.
    # This section will be in the ELF but won't be loaded into memory by PT_LOAD segments.
    set(METADATA_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_metadata.S")
    file(WRITE ${METADATA_FILE}
            ".section .driver_name, \"\"\n"
            ".asciz \"${TARGET_NAME}\"\n"
    )

    add_executable(${TARGET_NAME} ${SOURCES} ${METADATA_FILE})
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/kernel/src)
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
