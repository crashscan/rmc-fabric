# Inventory-specific transport implementations
# These are not reusable and should not be moved to lib/

add_library(inventory_transport STATIC
    DbusInventoryTransport.cpp
    InventoryDbusAdapter.cpp
    InventoryDbusCodec.cpp
    StdoutInventoryTransport.cpp
    TransportFactory.cpp
)

target_include_directories(inventory_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(inventory_transport
    PUBLIC
        dbus_transport_base
        interop_contract
        inventory_service_api
        inventory_core
    PRIVATE
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)
