add_executable(KernelTests
        str_test.cpp
        array_test.cpp
        expected_test.cpp
        unique_ptr_test.cpp
        static_identifier_test.cpp
        device_type_test.cpp
        format_test.cpp
        resource_arbiter_test.cpp
        panic.cpp
        volume_manager_test.cpp
        fat_short_name_test.cpp

        kernel_host_stubs.cpp
        kernel_host_vmm_stubs.cpp
        ../../kernel/src/object_manager.cpp
        ../../kernel/src/io.cpp
        ../../kernel/src/vfs.cpp
        ../../kernel/src/vmm.cpp
        ../../kernel/src/memory/Allocator.cpp
        ../../kernel/src/memory/Cache.cpp
        ../../kernel/src/memory/Layout.cpp
        ../../kernel/src/memory/Slab.cpp
        ../../kernel/src/memory/Storage.cpp

        object_manager_test.cpp
        subsystem_object_test.cpp
        vmm_test.cpp

        ../../drivers/disk_fake/fake_disk_file_backend.cpp
        disk_fake_test.cpp

        vfs_test.cpp

        vfs_fat_integration_test.cpp
)

target_include_directories(KernelTests PRIVATE ../../kernel/src ../../drivers/disk_fake)

# object_manager.cpp/io.cpp/vfs.cpp already work around the hosted
# placement-new visibility gap via stdkrn.hpp; vmm.cpp and memory/*.cpp
# instead include libkcpp's freestanding-only new.hpp, so give the whole
# target the same fix used for the driver modules below.
target_compile_options(KernelTests PRIVATE -include new)

target_link_libraries(KernelTests PRIVATE libkcpp KernelHeaders undos_warnings)
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

