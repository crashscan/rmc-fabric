add_library(inventory_transport STATIC
    src/dbus/DbusTransportBase.cpp
    src/dbus/DbusServiceAdapter.cpp
    src/DbusInventoryTransport.cpp
    src/inventory/InventoryDbusAdapter.cpp
    src/StdoutInventoryTransport.cpp
    src/TransportFactory.cpp
    src/inventory/InventoryDbusCodec.cpp
)

target_include_directories(inventory_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(inventory_transport
    PUBLIC
        interop_contract
        inventory_core
        inventory_service_api
    PRIVATE
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)