
add_executable(SDKGuiTests
    sdk_gui_test.cpp
    ../../../SDK/GUI/windows.c
    ../../../SDK/GUI/utf8.c
    ../../../SDK/GUI/ui.c)
target_include_directories(SDKGuiTests PRIVATE
    ../../../SDK/GUI/include ../../../SDK/GUI ../../../CRT/include
    )
target_compile_definitions(SDKGuiTests PRIVATE UNDOSGUI_TESTING)
set_target_properties(SDKGuiTests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(SDKGuiTests PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror>)
target_link_libraries(SDKGuiTests PRIVATE KernelUserlandShared Catch2::Catch2WithMain)
catch_discover_tests(SDKGuiTests)
