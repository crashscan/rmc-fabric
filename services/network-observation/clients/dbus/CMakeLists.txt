add_library(network_observation_client STATIC
    DbusClient.cpp
)
target_compile_features(network_observation_client PUBLIC cxx_std_20)
target_include_directories(network_observation_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../transports/dbus>
        $<INSTALL_INTERFACE:include/network-observation/transports/dbus>
)
target_link_libraries(network_observation_client
    PUBLIC
        interop_contract
        network_observation_dbus_codec
    PRIVATE
        dbus_client_support
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)
