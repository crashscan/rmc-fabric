add_library(dbus_transport_base STATIC
    src/DbusTransportBase.cpp
    src/DbusServiceAdapter.cpp
)

target_include_directories(dbus_transport_base
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbus_transport_base
    PUBLIC
        inventory_service_api
    PRIVATE
        DBusCxx::DBusCxx
        glog
)
