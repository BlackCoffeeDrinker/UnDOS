add_test(NAME libc_streams
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/tests/check-libc.py
            --cc ${CMAKE_C_COMPILER} --cxx ${CMAKE_CXX_COMPILER})

add_test(NAME syscall_idl
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tests/test_idlgen.py)

add_executable(KernelTests
        str_test.cpp
        array_test.cpp
        vector_test.cpp
        list_test.cpp
        map_test.cpp
        expected_test.cpp
        unique_ptr_test.cpp
        unique_lock_test.cpp
        static_identifier_test.cpp
        device_type_test.cpp
        format_test.cpp
        resource_arbiter_test.cpp
        panic.cpp
        volume_manager_test.cpp
        fat_short_name_test.cpp
        scheduler_test.cpp
        cpu_context_test.cpp
        wait_test.cpp
        user_test.cpp
        progman_test.cpp
        process_startup_test.cpp
        application_abi_test.cpp
        standard_handles_test.cpp
        font_resource_test.cpp
        font_loader_test.cpp
        ../../userland/fontloader/decoder.cpp
        percpu_test.cpp
        input_scancode_test.cpp
        mouse_packet_test.cpp
        input_queue_test.cpp
        hid_parser_test.cpp
        ps2_byte_ring_test.cpp
        gamepad_state_test.cpp
        ../../drivers/input_hid/hid_parser.cpp
        dispatch_interrupt_test.cpp
        event_broadcaster_test.cpp

        kernel_host_stubs.cpp
        kernel_host_vmm_stubs.cpp
        ../../kernel/src/object_manager.cpp
        ../../kernel/src/io.cpp
        ../../kernel/src/vfs.cpp
        ../../kernel/src/vmm.cpp
        ../../kernel/src/percpu.cpp
        ../../kernel/src/scheduler/CpuContext.cpp
        ../../kernel/src/memory/Allocator.cpp
        ../../kernel/src/memory/Cache.cpp
        ../../kernel/src/memory/Layout.cpp
        ../../kernel/src/memory/Slab.cpp
        ../../kernel/src/memory/Storage.cpp
        ../../kernel/src/configuration_manager.cpp
        ../../kernel/src/configuration_manager_io.cpp
        ../../kernel/src/event_broadcaster.cpp
        ../../kernel/src/gfx/gfx_management.cpp
        ../../kernel/src/gfx/gfx_software_fallback.cpp
        ../../kernel/src/gfx/gfx_dc_table.cpp
        ../../kernel/src/gfx/font/font_resources.cpp
        ../../kernel/src/gfx/monitor_config.cpp
        ../../kernel/src/tty/tty.cpp
        ../../kernel/src/tty/tty_device.cpp
        ../../kernel/src/user/window.cpp
        ../../kernel/src/user/msgqueue.cpp
        ../../kernel/src/user/paint.cpp
        ../../kernel/src/user/nonclient.cpp

        object_manager_test.cpp
        subsystem_object_test.cpp
        vmm_test.cpp
        object_creation_leak_test.cpp
        configuration_manager_test.cpp
        gfx_test.cpp
        gfx_bitmap_test.cpp
        tty_test.cpp
        io_object_test.cpp

        ../../drivers/disk_fake/fake_disk_file_backend.cpp
        disk_fake_test.cpp

        vfs_test.cpp

        vfs_fat_integration_test.cpp
)

target_include_directories(KernelTests PRIVATE ../../kernel/src ../../drivers/disk_fake
        ../../kernel_userland_shared/include
        ../../userland/fontloader
        ../../drivers/input_hid ../../drivers/input_i8042 ../../drivers/input_gameport)

# object_manager.cpp/io.cpp/vfs.cpp already work around the hosted
# placement-new visibility gap via stdkrn.hpp; vmm.cpp and memory/*.cpp
# instead include libkcpp's freestanding-only new.hpp, so give the whole
# target the same fix used for the driver modules below.
target_compile_options(KernelTests PRIVATE -include new)

target_link_libraries(KernelTests PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_libraries(KernelTests PRIVATE undos_font_resource)
target_link_libraries(KernelTests PRIVATE Catch2::Catch2WithMain)

# The real fs_fat/volmgr driver sources define their own `extern "C" DriverEntry`,
# which would collide if linked directly into KernelTests alongside each other.
# Instead, build each as a standalone loadable module (mirroring how the real
# kernel loads drivers as separate ELF images) and dlopen() it from the
# end-to-end integration test. KE_OB_*/KE_IO_*/KE_VFS_* symbols are left
# unresolved at link time and are looked up dynamically against KernelTests'
# own symbol table when the module is loaded, so both share the same
# process-wide object manager / VFS state.
add_library(fake_fs_fat_driver MODULE ../../drivers/fs_fat/fat.cpp)
target_link_libraries(fake_fs_fat_driver PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_options(fake_fs_fat_driver PRIVATE -undefined dynamic_lookup)
# Same hosted-build placement-new visibility issue as stdkrn.hpp: these driver
# sources use placement new via headers that don't pull in <new> themselves.
target_compile_options(fake_fs_fat_driver PRIVATE -include new)
# The project sets CMAKE_CXX_VISIBILITY_PRESET hidden globally, which would
# hide DriverEntry from dlsym(); override it back to default for this module.
set_target_properties(fake_fs_fat_driver PROPERTIES CXX_VISIBILITY_PRESET default)

add_library(fake_volmgr_driver MODULE ../../drivers/volmgr/volmgr.cpp)
target_link_libraries(fake_volmgr_driver PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_options(fake_volmgr_driver PRIVATE -undefined dynamic_lookup)
target_compile_options(fake_volmgr_driver PRIVATE -include new)
set_target_properties(fake_volmgr_driver PROPERTIES CXX_VISIBILITY_PRESET default)

target_compile_definitions(KernelTests PRIVATE
        FS_FAT_DRIVER_PATH="$<TARGET_FILE:fake_fs_fat_driver>"
        VOLMGR_DRIVER_PATH="$<TARGET_FILE:fake_volmgr_driver>")
target_link_libraries(KernelTests PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(KernelTests fake_fs_fat_driver fake_volmgr_driver)

catch_discover_tests(KernelTests)
add_test(NAME process_startup_trampoline
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/process_startup_trampoline_test.py
        --build-dir ${CMAKE_BINARY_DIR})
