add_executable(network-observationd
    main.cpp
    ObservationService.cpp
    ${TRANSPORT_BASE}/TransportFactory.cpp
    ${TRANSPORT_BASE}/dbus/DbusTransport.cpp
    ${TRANSPORT_BASE}/dbus/NetworkObservationDbusAdapter.cpp
    ${TRANSPORT_BASE}/stdout/StdoutTransport.cpp
)

target_include_directories(network-observationd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TRANSPORT_BASE}
    ${TRANSPORT_BASE}/dbus
    ${TRANSPORT_BASE}/stdout
    ${NETLINK_BASE}/types
    ${OBS_BASE}/public
    ${OBS_BASE}/engine
    ${OBS_BASE}/classifier
    ${OBS_BASE}/policy
    ${GLOG_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_libraries(network-observationd
    PRIVATE
        dbus_transport_base
        interop_contract
        dbus_client_support
        daemon_support
        DBusCxx::DBusCxx
        SigCxx::SigCxx
    netlink-monitor
    observation-model
    pthread
    glog
    gflags
    jsoncpp
    liblldpctl.so
)

add_dependencies(network-observationd observation-model )
