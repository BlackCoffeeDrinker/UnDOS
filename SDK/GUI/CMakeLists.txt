add_library(undosgui STATIC windows.c windows_core.c windows_message.c windows_paint.c gdi.c bitmap.c resource.c menu.c windows_tree.c input.c mdi.c file.c shell.c kernel32_string.c kernel32_thread.c user32_helpers.c dialog.c controls/common.c controls/button.c controls/static.c controls/edit.c controls/listbox.c controls/scrollbar.c utf8.c ui.c windows_backend.c)
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
