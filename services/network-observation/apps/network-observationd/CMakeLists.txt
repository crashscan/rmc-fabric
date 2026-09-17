add_executable(network-observationd
        main.cpp
)

target_link_libraries(network-observationd
        PRIVATE
        network_observation_service
        network_observation_transport_factory
        rsc_util::service_runtime
        pthread
        glog
        gflags
        jsoncpp
        liblldpctl.so
)

install(
        TARGETS network-observationd
        RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
        COMPONENT rel
)
