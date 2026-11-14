
add_executable(SDKGuiTests
    sdk_gui_test.cpp
    ../../../SDK/GUI/windows.c
    ../../../SDK/GUI/windows_core.c
    ../../../SDK/GUI/windows_message.c
    ../../../SDK/GUI/windows_paint.c
    ../../../SDK/GUI/gdi.c
    ../../../SDK/GUI/bitmap.c
    ../../../SDK/GUI/resource.c
    ../../../SDK/GUI/menu.c
    ../../../SDK/GUI/windows_tree.c
    ../../../SDK/GUI/input.c
    ../../../SDK/GUI/mdi.c
    ../../../SDK/GUI/file.c
    ../../../SDK/GUI/shell.c
    ../../../SDK/GUI/kernel32_string.c
    ../../../SDK/GUI/kernel32_thread.c
    ../../../SDK/GUI/user32_helpers.c
    ../../../SDK/GUI/dialog.c
    ../../../SDK/GUI/controls/common.c
    ../../../SDK/GUI/controls/button.c
    ../../../SDK/GUI/controls/static.c
    ../../../SDK/GUI/controls/edit.c
    ../../../SDK/GUI/controls/listbox.c
    ../../../SDK/GUI/controls/scrollbar.c
    ../../../SDK/GUI/utf8.c
    ../../../SDK/GUI/ui.c
    ../../../Apps/FileManager/undos/resources.c
    ../../../Apps/FileManager/undos/mounts.c)
target_include_directories(SDKGuiTests PRIVATE
    ../../../SDK/GUI/include ../../../SDK/GUI ../../../CRT/include
    ../../../Apps/FileManager/undos
    )
target_compile_definitions(SDKGuiTests PRIVATE UNDOSGUI_TESTING)
set_target_properties(SDKGuiTests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(SDKGuiTests PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror>)
target_link_libraries(SDKGuiTests PRIVATE KernelUserlandShared Catch2::Catch2WithMain)
catch_discover_tests(SDKGuiTests)
