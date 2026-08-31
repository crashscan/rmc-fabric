add_executable(network-observationd
    main.cpp
)

target_link_libraries(network-observationd
    PRIVATE
        network_observation_service
        network_observation_transport_factory
        daemon_support
        pthread
        glog
        gflags
        jsoncpp
        liblldpctl.so
)
