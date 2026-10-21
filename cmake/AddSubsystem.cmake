# add_subsystem(<target>
#     OUTPUT_NAME <8.3 base name>   # default: first 8 characters of <target>
#     START <Boot|Automatic|LastResort>  # default: Automatic
#     DEPENDS <subsystem target>...      # modules loaded first
#     SOURCES <files...>)
#
# Builds a relocatable kernel-space module whose entry point is
# `SubsystemEntry(const KObjectPtr<KModuleObject> &)`. The module name (the
# target name, also its registry key /Subsystems/<target>) and its dependencies
# are embedded in a ".note.undos.module" ELF note. Symbols marked
# UNDOS_MODULE_EXPORT (default visibility) become the module's exports, read
# from the image's symbol table, so the image must not be stripped.
# The image is staged as System/Subsys/<OUTPUT_NAME>.elf by the
# make-initial-disk target.
function(add_subsystem TARGET_NAME)
    cmake_parse_arguments(SUBSYS "" "OUTPUT_NAME;START" "SOURCES;DEPENDS" ${ARGN})
    if (NOT SUBSYS_SOURCES)
        message(FATAL_ERROR "add_subsystem(${TARGET_NAME}): SOURCES is required")
    endif ()
    if (NOT SUBSYS_START)
        set(SUBSYS_START Automatic)
    endif ()
    if (NOT SUBSYS_START MATCHES "^(Boot|Automatic|LastResort)$")
        message(FATAL_ERROR "add_subsystem(${TARGET_NAME}): invalid START '${SUBSYS_START}'")
    endif ()
    if (NOT SUBSYS_OUTPUT_NAME)
        string(SUBSTRING ${TARGET_NAME} 0 8 SUBSYS_OUTPUT_NAME)
    endif ()
    # FAT short names have no truncation fallback.
    string(LENGTH "${SUBSYS_OUTPUT_NAME}" SUBSYS_OUTPUT_LENGTH)
    if (SUBSYS_OUTPUT_LENGTH GREATER 8)
        message(FATAL_ERROR "add_subsystem(${TARGET_NAME}): OUTPUT_NAME '${SUBSYS_OUTPUT_NAME}' exceeds 8 characters")
    endif ()

    # Note descriptor: "<name>\0<dependency>\0...\0", owner "UnDOS", type 1.
    set(NOTE_DESC ".asciz \"${TARGET_NAME}\"\n")
    foreach (dependency ${SUBSYS_DEPENDS})
        string(APPEND NOTE_DESC ".asciz \"${dependency}\"\n")
    endforeach ()
    set(METADATA_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_module_note.S")
    file(WRITE ${METADATA_FILE}
            ".section .note.undos.module, \"\", @note\n"
            ".balign 4\n"
            ".long 6\n"
            ".long desc_end - desc_start\n"
            ".long 1\n"
            ".asciz \"UnDOS\"\n"
            ".balign 4\n"
            "desc_start:\n"
            "${NOTE_DESC}"
            ".byte 0\n"
            "desc_end:\n"
            ".balign 4\n"
    )

    add_executable(${TARGET_NAME} ${SUBSYS_SOURCES} ${METADATA_FILE})
    if (SUBSYS_DEPENDS)
        add_dependencies(${TARGET_NAME} ${SUBSYS_DEPENDS})
    endif ()
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/kernel/src/syscalls
            ${CMAKE_SOURCE_DIR}/kernel_userland_shared/include)
    target_link_libraries(${TARGET_NAME}
            PRIVATE
            libkcpp
            KernelHeaders
            undos_options undos_warnings
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE UNDOS_DRIVER_COMPILE UNDOS_SUBSYSTEM_COMPILE)
    target_compile_options(${TARGET_NAME} PRIVATE -fvisibility=hidden)
    target_link_options(${TARGET_NAME}
            PRIVATE
            -fvisibility=hidden
            "-T${CMAKE_SOURCE_DIR}/subsystems/subsystem.ld"
            -nostdlib
            -static
            -Wl,--emit-relocs
            -Wl,--unresolved-symbols=ignore-all
            -Wl,--no-warn-rwx-segments
    )

    set_property(GLOBAL APPEND PROPERTY UNDOS_SUBSYSTEMS ${TARGET_NAME})
    set_property(GLOBAL PROPERTY UNDOS_SUBSYSTEM_${TARGET_NAME}_FILE ${SUBSYS_OUTPUT_NAME}.elf)
    set_property(GLOBAL PROPERTY UNDOS_SUBSYSTEM_${TARGET_NAME}_START ${SUBSYS_START})
endfunction()
