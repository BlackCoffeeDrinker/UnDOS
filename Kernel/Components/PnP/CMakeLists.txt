# PnP: device tree, driver binding, configuration hive and resource manager.
add_library(kernel_pnp OBJECT
        src/pnp_manager.cpp
        src/pnp_device_state.cpp
        src/configuration_manager.cpp
        src/configuration_manager_io.cpp
        src/resource_manager.cpp
)
undos_kernel_component(kernel_pnp PnP)
target_link_libraries(kernel_pnp PRIVATE Executive_Interface)
