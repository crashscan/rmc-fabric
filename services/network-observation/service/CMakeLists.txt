add_subdirectory(ports)

add_library(network_observation_service STATIC
    ObservationService.cpp
    NetworkObservationAdapter.cpp
)

target_compile_features(network_observation_service PUBLIC cxx_std_20)

target_include_directories(network_observation_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ports>
        $<INSTALL_INTERFACE:include/network-observation/service>
)
target_include_directories(network_observation_service
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/engine>
)

target_link_libraries(network_observation_service
    PUBLIC
        observation-model
        network_observation_service_ports
        service_framework
    PRIVATE
        lldp-observer
        netlink-monitor
        glog
)

if(BUILD_TESTING)
    add_executable(network_observation_service_tests
        tests/ObservationServiceTests.cpp
    )
    target_compile_features(network_observation_service_tests PRIVATE cxx_std_20)
    target_link_libraries(network_observation_service_tests
        PRIVATE
            network_observation_service
            pthread
    )
    add_test(NAME network_observation_service_tests COMMAND network_observation_service_tests)
endif()
