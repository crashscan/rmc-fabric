add_library(inventory_service_ports INTERFACE)
target_compile_features(inventory_service_ports INTERFACE cxx_std_20)

target_include_directories(inventory_service_ports
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/rmc-inventory/api>
)

target_link_libraries(inventory_service_ports INTERFACE interop_contract inventory_core service_framework)
