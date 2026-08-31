find_package(SigCxx REQUIRED MODULE)
find_package(DBusCxx REQUIRED MODULE)

add_library(network_observation_dbus_codec STATIC
    dbus/NetworkObservationDbusCodec.cpp
)
target_compile_features(network_observation_dbus_codec PUBLIC cxx_std_20)
target_include_directories(network_observation_dbus_codec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dbus>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/network_observation>
)
target_link_libraries(network_observation_dbus_codec
    PUBLIC
        interop_contract
    PRIVATE
        DBusCxx::DBusCxx
)

add_library(network_observation_dbus_transport STATIC
    dbus/DbusTransport.cpp
    dbus/NetworkObservationDbusAdapter.cpp
)
target_compile_features(network_observation_dbus_transport PUBLIC cxx_std_20)
target_include_directories(network_observation_dbus_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dbus>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../service/ports>
        $<INSTALL_INTERFACE:include/network-observation/transports>
)
target_link_libraries(network_observation_dbus_transport
    PUBLIC
        network_observation_service_ports
        network_observation_dbus_codec
        dbus_transport_base
    PRIVATE
        service_framework
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)

add_library(network_observation_stdout_transport STATIC
    stdout/StdoutTransport.cpp
)
target_compile_features(network_observation_stdout_transport PUBLIC cxx_std_20)
target_include_directories(network_observation_stdout_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stdout>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../service/ports>
        $<INSTALL_INTERFACE:include/network-observation/transports/stdout>
)
target_link_libraries(network_observation_stdout_transport
    PUBLIC
        network_observation_service_ports
)

add_library(network_observation_transport_factory STATIC
    TransportFactory.cpp
)
target_compile_features(network_observation_transport_factory PUBLIC cxx_std_20)
target_include_directories(network_observation_transport_factory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../service/ports>
        $<INSTALL_INTERFACE:include/network-observation/transports>
)
target_link_libraries(network_observation_transport_factory
    PUBLIC
        network_observation_service_ports
        network_observation_dbus_transport
        network_observation_stdout_transport
)

if(BUILD_TESTING)
    add_executable(network_observation_dbus_codec_tests
        dbus/tests/NetworkObservationDbusCodecTests.cpp
    )
    target_compile_features(network_observation_dbus_codec_tests PRIVATE cxx_std_20)
    target_link_libraries(network_observation_dbus_codec_tests
        PRIVATE
            network_observation_dbus_codec
            DBusCxx::DBusCxx
    )
    add_test(NAME network_observation_dbus_codec_tests COMMAND network_observation_dbus_codec_tests)
    set_tests_properties(network_observation_dbus_codec_tests PROPERTIES TIMEOUT 10 LABELS "unit")
endif()
install(
    TARGETS network_observation_dbus_codec
    EXPORT  rmc_fabricTargets
    ARCHIVE COMPONENT dev
)
