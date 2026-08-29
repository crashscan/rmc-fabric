add_executable(net-observe
    main.cpp
    ${TRANSPORT_BASE}/dbus/DbusClient.cpp
)

target_include_directories(net-observe PRIVATE
    ${TRANSPORT_BASE}/dbus
    ${OBS_BASE}/public
    ${GLOG_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_libraries(net-observe
    PRIVATE
        interop_contract
        dbus_client_support
        DBusCxx::DBusCxx
        SigCxx::SigCxx
    pthread
    glog
    gflags
    jsoncpp
)
add_dependencies(net-observe observation-model )
