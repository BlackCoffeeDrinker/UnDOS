add_library(abi_test_shared_object OBJECT shared.cpp ../../abi_note.S)
target_link_libraries(abi_test_shared_object PRIVATE KernelUserlandShared)
target_compile_options(abi_test_shared_object PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fPIC -m32 -fvisibility=default)
set(ABI_SHARED_FILE ${CMAKE_CURRENT_BINARY_DIR}/abitest.so)
add_custom_command(OUTPUT ${ABI_SHARED_FILE}
    COMMAND ${CMAKE_LINKER} -shared --hash-style=sysv -soname abitest.so
            -o ${ABI_SHARED_FILE} $<TARGET_OBJECTS:abi_test_shared_object>
    DEPENDS abi_test_shared_object $<TARGET_OBJECTS:abi_test_shared_object>
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(abi_test_shared DEPENDS ${ABI_SHARED_FILE})

add_library(abi_test_dependent_object OBJECT dependent.cpp ../../abi_note.S)
target_link_libraries(abi_test_dependent_object PRIVATE KernelUserlandShared)
target_compile_options(abi_test_dependent_object PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fPIC -m32 -fvisibility=default)
set(ABI_DEPENDENT_FILE ${CMAKE_CURRENT_BINARY_DIR}/abinext.so)
add_custom_command(OUTPUT ${ABI_DEPENDENT_FILE}
    COMMAND ${CMAKE_LINKER} -shared --hash-style=sysv -soname abinext.so
            -o ${ABI_DEPENDENT_FILE} $<TARGET_OBJECTS:abi_test_dependent_object> ${ABI_SHARED_FILE}
    DEPENDS abi_test_dependent_object $<TARGET_OBJECTS:abi_test_dependent_object> ${ABI_SHARED_FILE}
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(abi_test_dependent DEPENDS ${ABI_DEPENDENT_FILE})

add_executable(abi_test main.cpp ../../abi_note.S)
target_link_libraries(abi_test PRIVATE undoscrt)
target_compile_options(abi_test PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fno-pic -fno-pie -fuse-cxa-atexit -m32)
target_link_options(abi_test PRIVATE -nostdlib -no-pie -Wl,--export-dynamic -Wl,--hash-style=sysv -Wl,--no-dynamic-linker
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/../term/linker.ld ${ABI_SHARED_FILE} ${ABI_DEPENDENT_FILE})
set_target_properties(abi_test PROPERTIES POSITION_INDEPENDENT_CODE OFF)
set_property(TARGET abi_test APPEND PROPERTY LINK_DEPENDS ${ABI_SHARED_FILE} ${ABI_DEPENDENT_FILE})
set_property(TARGET abi_test APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../term/linker.ld)
add_dependencies(abi_test abi_test_shared abi_test_dependent)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf
    COMMAND ${CMAKE_OBJCOPY} --remove-section .note.undos $<TARGET_FILE:abi_test>
            ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf
    DEPENDS abi_test VERBATIM)
add_custom_target(abi_test_missing_note DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/abimiss.elf)
