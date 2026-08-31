add_library(network_observation_service STATIC
    ObservationService.cpp
)

target_compile_features(network_observation_service PUBLIC cxx_std_20)

target_include_directories(network_observation_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/network-observation/service>
)

target_link_libraries(network_observation_service
    PUBLIC
        observation-model
        network_observation_transport_port
        service_framework
)

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
