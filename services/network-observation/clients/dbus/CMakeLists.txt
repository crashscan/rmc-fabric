add_library(network_observation_client STATIC
    DbusClient.cpp
)
target_compile_features(network_observation_client PUBLIC cxx_std_20)
target_include_directories(network_observation_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/network_observation>
)
target_link_libraries(network_observation_client
    PUBLIC
        interop_contract
    PRIVATE
        network_observation_dbus_codec
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)

install(
    TARGETS network_observation_client
    EXPORT  rmc_fabricTargets
    ARCHIVE COMPONENT dev
)

install(
    FILES   ${CMAKE_CURRENT_SOURCE_DIR}/DbusClient.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/network_observation
    COMPONENT   dev
)
