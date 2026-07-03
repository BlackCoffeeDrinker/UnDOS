add_executable(KernelTests
        str_test.cpp
        array_test.cpp
        expected_test.cpp
        unique_ptr_test.cpp
        static_identifier_test.cpp
        device_type_test.cpp
        panic.cpp
)

target_link_libraries(KernelTests PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_libraries(KernelTests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(KernelTests)

