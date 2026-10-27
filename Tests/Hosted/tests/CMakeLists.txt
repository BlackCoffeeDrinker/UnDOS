add_test(NAME libc_streams
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../SDK/tests/check-libc.py
            --cc ${CMAKE_C_COMPILER} --cxx ${CMAKE_CXX_COMPILER})

add_test(NAME syscall_idl
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../Tools/tests/test_idlgen.py)

add_executable(KernelTests
        str_test.cpp
        array_test.cpp
        vector_test.cpp
        list_test.cpp
        map_test.cpp
        expected_test.cpp
        kernel_error_test.cpp
        export_table_test.cpp
        unique_ptr_test.cpp
        unique_lock_test.cpp
        static_identifier_test.cpp
        device_type_test.cpp
        format_test.cpp
        resource_arbiter_test.cpp
        panic.cpp
        volume_manager_test.cpp
        root_volume_selector_test.cpp
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
        font_config_test.cpp
        font_corpus_test.cpp
        font_glyph_test.cpp
        font_raster_test.cpp
        font_face_test.cpp
        font_scaler_test.cpp
        font_vm_test.cpp
        ../../../Kernel/Source/src/gfx/font/glyph.cpp
        ../../../Kernel/Source/src/gfx/font/raster.cpp
        ../../../Kernel/Source/src/gfx/font/face.cpp
        ../../../Kernel/Source/src/gfx/font/realization.cpp
        ../../../Kernel/Source/src/gfx/font/default_font.cpp
        ../../../Kernel/Source/src/gfx/font/text.cpp
        ../../../Kernel/Source/src/gfx/font/font_system.cpp
        ../../../Kernel/Source/src/gfx/font/vm.cpp
        ../../../Kernel/Source/src/gfx/font/vm_executor.cpp
        ../../../Kernel/Source/src/gfx/font/vm_points.cpp
        ../../../Kernel/Source/src/gfx/font/vm_hints.cpp
        ../../../Apps/Fontloader/decoder.cpp
        percpu_test.cpp
        input_scancode_test.cpp
        mouse_packet_test.cpp
        input_queue_test.cpp
        hid_parser_test.cpp
        ps2_byte_ring_test.cpp
        gamepad_state_test.cpp
        ../../../Kernel/Drivers/input_hid/hid_parser.cpp
        dispatch_interrupt_test.cpp
        event_broadcaster_test.cpp

        kernel_host_stubs.cpp
        kernel_host_vmm_stubs.cpp
        ../../../Kernel/Source/src/object_manager.cpp
        ../../../Kernel/Source/src/initial/root_volume_selector.cpp
        ../../../Kernel/Source/src/kerror.cpp
        ../../../Kernel/Source/src/syscalls/KErrorToSysReturnCode.cpp
        ../../../Kernel/Source/src/io.cpp
        ../../../Kernel/Source/src/vfs.cpp
        ../../../Kernel/Source/src/vmm.cpp
        ../../../Kernel/Source/src/percpu.cpp
        ../../../Kernel/Source/src/scheduler/CpuContext.cpp
        ../../../Kernel/Source/src/memory/Allocator.cpp
        ../../../Kernel/Source/src/memory/Cache.cpp
        ../../../Kernel/Source/src/memory/Layout.cpp
        ../../../Kernel/Source/src/memory/Slab.cpp
        ../../../Kernel/Source/src/memory/Storage.cpp
        ../../../Kernel/Source/src/configuration_manager.cpp
        ../../../Kernel/Source/src/configuration_manager_io.cpp
        ../../../Kernel/Source/src/event_broadcaster.cpp
        ../../../Kernel/Source/src/gfx/gfx_management.cpp
        ../../../Kernel/Source/src/gfx/gfx_software_fallback.cpp
        ../../../Kernel/Source/src/gfx/gfx_dc_table.cpp
        ../../../Kernel/Source/src/gfx/font/font_resources.cpp
        ../../../Kernel/Source/src/gfx/monitor_config.cpp
        ../../../Kernel/Source/src/tty/tty.cpp
        ../../../Kernel/Source/src/tty/tty_device.cpp
        ../../../Kernel/Source/src/user/window.cpp
        ../../../Kernel/Source/src/user/menu.cpp
        ../../../Kernel/Source/src/user/msgqueue.cpp
        ../../../Kernel/Source/src/user/paint.cpp
        ../../../Kernel/Source/src/user/nonclient.cpp
        ../../../Kernel/Source/src/user/rawinput.cpp
        ../../../Kernel/Source/src/syscalls/GFX.cpp

        object_manager_test.cpp
        subsystem_object_test.cpp
        vmm_test.cpp
        object_creation_leak_test.cpp
        configuration_manager_test.cpp
        gfx_test.cpp
        gfx_bitmap_test.cpp
        tty_test.cpp
        io_object_test.cpp

        ../../../Kernel/Drivers/disk_fake/fake_disk_file_backend.cpp
        disk_fake_test.cpp
        ata_read_test.cpp

        vfs_test.cpp

        vfs_fat_integration_test.cpp
)

target_include_directories(KernelTests PRIVATE ../../../Kernel/Source/src ../../../Kernel/Drivers/disk_fake
        "${UNDOS_SYSCALL_GENERATED_DIR}/kernel" "${UNDOS_SYSCALL_KERNEL_DIR}"
        ../../../Apps/Fontloader
        ../../../Kernel/Drivers/input_hid ../../../Kernel/Drivers/input_i8042 ../../../Kernel/Drivers/input_gameport)

# object_manager.cpp/io.cpp/vfs.cpp already work around the hosted
# placement-new visibility gap via stdkrn.hpp; vmm.cpp and memory/*.cpp
# instead include libkcpp's freestanding-only new.hpp, so give the whole
# target the same fix used for the driver modules below.
target_compile_options(KernelTests PRIVATE -include new)

target_link_libraries(KernelTests PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_libraries(KernelTests PRIVATE undos_font_resource)
target_link_libraries(KernelTests PRIVATE Catch2::Catch2WithMain)

set(_font_corpus "#pragma once\n#include <stdint.h>\nnamespace font_fixture {\n")
foreach (_font SourceSans3-Regular SourceSans3-Bold SourceCodePro-Regular)
    set(_font_path "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts/${_font}.ttf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_font_path}")
    file(READ "${_font_path}" _font_bytes HEX)
    string(REGEX REPLACE "(..)" "0x\\1," _font_bytes "${_font_bytes}")
    string(REPLACE "-" "_" _font_symbol "${_font}")
    string(APPEND _font_corpus "inline constexpr uint8_t ${_font_symbol}[] = {${_font_bytes}};\n")
endforeach ()
string(APPEND _font_corpus "}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/font_corpus.hpp" CONTENT "${_font_corpus}")
target_include_directories(KernelTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# The real fs_fat/volmgr driver sources define their own `extern "C" DriverEntry`,
# which would collide if linked directly into KernelTests alongside each other.
# Instead, build each as a standalone loadable module (mirroring how the real
# kernel loads drivers as separate ELF images) and dlopen() it from the
# end-to-end integration test. KE_OB_*/KE_IO_*/KE_VFS_* symbols are left
# unresolved at link time and are looked up dynamically against KernelTests'
# own symbol table when the module is loaded, so both share the same
# process-wide object manager / VFS state.
add_library(fake_fs_fat_driver MODULE ../../../Kernel/Drivers/fs_fat/fat.cpp)
target_link_libraries(fake_fs_fat_driver PRIVATE libkcpp KernelHeaders undos_warnings)
target_link_options(fake_fs_fat_driver PRIVATE -undefined dynamic_lookup)
# Same hosted-build placement-new visibility issue as stdkrn.hpp: these driver
# sources use placement new via headers that don't pull in <new> themselves.
target_compile_options(fake_fs_fat_driver PRIVATE -include new)
# The project sets CMAKE_CXX_VISIBILITY_PRESET hidden globally, which would
# hide DriverEntry from dlsym(); override it back to default for this module.
set_target_properties(fake_fs_fat_driver PROPERTIES CXX_VISIBILITY_PRESET default)

add_library(fake_volmgr_driver MODULE ../../../Kernel/Drivers/volmgr/volmgr.cpp)
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

add_executable(KernelPrintTests konsole_test.cpp)
target_compile_options(KernelPrintTests PRIVATE -include new)
target_link_libraries(KernelPrintTests PRIVATE libkcpp KernelHeaders undos_warnings Catch2::Catch2WithMain)
catch_discover_tests(KernelPrintTests)

add_executable(GrubDirectTests
        grub_direct_loader_test.cpp
        ../../../Boot/GrubDirect/core/loader_core.c
        ../../../Boot/GrubDirect/core/cpu_profile.c
        ../../../Boot/GrubDirect/core/diagnostics.c)
target_include_directories(GrubDirectTests PRIVATE ../../../Boot/GrubDirect/core)
target_link_libraries(GrubDirectTests PRIVATE Catch2::Catch2WithMain)
add_test(NAME GrubDirectTests COMMAND GrubDirectTests "[boot][grub-direct]")

add_executable(AtomicCmpxchgTests atomic_cmpxchg_test.cpp)
target_compile_definitions(AtomicCmpxchgTests PRIVATE
        UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS)
if (APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set_target_properties(AtomicCmpxchgTests PROPERTIES OSX_ARCHITECTURES x86_64)
endif ()
add_test(NAME AtomicCmpxchgTests COMMAND AtomicCmpxchgTests)
set_tests_properties(AtomicCmpxchgTests PROPERTIES TIMEOUT 15)

add_test(NAME process_startup_trampoline
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/process_startup_trampoline_test.py
        --build-dir ${CMAKE_BINARY_DIR})
