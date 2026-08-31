add_library(inventory-client STATIC
        src/InventoryClient.cpp
)

target_compile_features(inventory-client PUBLIC cxx_std_20)

target_include_directories(inventory-client PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/rmc-inventory/inventory-client>
)

target_link_libraries(inventory-client
    PUBLIC
        interop_contract
        inventory_dbus_codec
    PRIVATE
        DBusCxx::DBusCxx
        SigCxx::SigCxx
)