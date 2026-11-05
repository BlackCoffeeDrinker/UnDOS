# GFX (GDI): device contexts, drawing, monitor configuration and fonts.
add_library(kernel_gfx OBJECT
        src/syscalls/GFX.cpp
        src/gfx/process_state.cpp
        src/gfx/gfx_management.cpp
        src/gfx/gfx_software_fallback.cpp
        src/gfx/gfx_dc_table.cpp
        src/gfx/font/font_resources.cpp
        src/gfx/font/glyph.cpp
        src/gfx/font/raster.cpp
        src/gfx/font/face.cpp
        src/gfx/font/realization.cpp
        src/gfx/font/default_font.cpp
        src/gfx/font/scaler.cpp
        src/gfx/font/text.cpp
        src/gfx/font/font_system.cpp
        src/gfx/font/vm.cpp
        src/gfx/font/vm_executor.cpp
        src/gfx/font/vm_points.cpp
        src/gfx/font/vm_hints.cpp
        src/gfx/monitor_config.cpp
)
undos_kernel_component(kernel_gfx)
# GFX still calls into USER (scene lock, cursor exclusion, display-change
# notifications), so it sees User's headers; untangling this is pending.
target_include_directories(kernel_gfx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../User/src")
