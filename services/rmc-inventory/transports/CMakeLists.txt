# Inventory-specific transport implementations
# These are not reusable and should not be moved to lib/

set(TRANSPORT_SOURCES
    DbusInventoryTransport.cpp
    InventoryDbusAdapter.cpp
    InventoryDbusCodec.cpp
    TransportFactory.cpp
)

if(INVENTORY_ENABLE_STDOUT_TRANSPORT)
    list(APPEND TRANSPORT_SOURCES StdoutInventoryTransport.cpp)
    add_compile_definitions(INVENTORY_ENABLE_STDOUT_TRANSPORT)
endif()

add_library(inventory_transport STATIC ${TRANSPORT_SOURCES})

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
        service_framework
    PRIVATE
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)
