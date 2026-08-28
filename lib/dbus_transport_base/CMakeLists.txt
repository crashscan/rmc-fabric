add_library(dbus_transport_base STATIC
    DbusTransportBase.cpp
    DbusServiceAdapter.cpp
)

target_include_directories(dbus_transport_base
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dbus_transport_base
    PRIVATE
        DBusCxx::DBusCxx
        glog
)
