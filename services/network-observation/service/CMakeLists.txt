add_subdirectory(ports)

add_library(
        network_observation_service
        STATIC
        ObservationService.cpp
        NetlinkLldpObservationRuntime.cpp
        NetlinkObservationMapper.cpp
)

target_compile_features(
        network_observation_service
        PUBLIC
        cxx_std_20
)

target_include_directories(
        network_observation_service
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ports>
        $<INSTALL_INTERFACE:include/network-observation/service>
)

target_link_libraries(
        network_observation_service
        PUBLIC
        network_observation_service_ports

        # ObservationService.h publicly derives ServiceBase and contains
        # LifecycleCoordinator and ManagedWorker.
        rsc_util::service_runtime
        PRIVATE
        observation-model
        lldp-observer
        netlink-monitor
        glog
)

if (BUILD_TESTING)
    add_executable(
            network_observation_service_tests
            tests/ObservationServiceTests.cpp
    )

    target_compile_features(
            network_observation_service_tests
            PRIVATE
            cxx_std_20
    )

    target_link_libraries(
            network_observation_service_tests
            PRIVATE
            network_observation_service
            pthread
    )

    add_test(
            NAME network_observation_service_tests
            COMMAND network_observation_service_tests
    )

    set_tests_properties(
            network_observation_service_tests
            PROPERTIES
            TIMEOUT 15
            LABELS "unit;resilience"
    )

    add_executable(network_observation_bounded_queue_tests tests/BoundedObservationQueueTests.cpp)
    target_compile_features(network_observation_bounded_queue_tests PRIVATE cxx_std_20)
    target_link_libraries(network_observation_bounded_queue_tests
            PRIVATE network_observation_service observation-model rsc_util pthread)
    add_test(NAME network_observation_bounded_queue_tests COMMAND network_observation_bounded_queue_tests)
    set_tests_properties(network_observation_bounded_queue_tests PROPERTIES TIMEOUT 15 LABELS "unit;resilience")

    # service/CMakeLists.txt, inside if(BUILD_TESTING):
    add_executable(network_observation_publication_queue_tests tests/PublicationQueueTests.cpp)
    target_compile_features(network_observation_publication_queue_tests PRIVATE cxx_std_20)
    target_link_libraries(network_observation_publication_queue_tests
            PRIVATE network_observation_service rsc_util pthread)
    add_test(NAME network_observation_publication_queue_tests
            COMMAND network_observation_publication_queue_tests)
    set_tests_properties(network_observation_publication_queue_tests
            PROPERTIES TIMEOUT 15 LABELS "unit;resilience")

    # Existing service tests unchanged.

    add_executable(network_observation_runtime_tests
            tests/NetlinkLldpObservationRuntimeTests.cpp
            tests/NetlinkObservationMapperTests.cpp
    )
    target_compile_features(network_observation_runtime_tests PRIVATE cxx_std_20)
    target_link_libraries(network_observation_runtime_tests
            PRIVATE
            network_observation_service
            observation-model      # FakeModel implements INetworkObservationModel
            lldp-observer          # FakeLldpSource implements ILldpSource
            netlink-monitor        # NetlinkTypes.h
            pthread
    )
    add_test(NAME network_observation_runtime_tests
            COMMAND network_observation_runtime_tests)
    set_tests_properties(network_observation_runtime_tests PROPERTIES
            TIMEOUT 30 LABELS "unit;resilience")
endif ()
