add_library(rmc_fabric_integration_support STATIC
    IntegrationSupport.cpp
)
target_compile_features(rmc_fabric_integration_support PUBLIC cxx_std_20)
target_include_directories(rmc_fabric_integration_support
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_executable(inventory_integration_tests
    InventoryIntegrationTests.cpp
)
target_compile_features(inventory_integration_tests PRIVATE cxx_std_20)
target_link_libraries(inventory_integration_tests
    PRIVATE
        rmc_fabric_integration_support
        inventory-client
)
add_test(
    NAME inventory_integration_tests
    COMMAND inventory_integration_tests $<TARGET_FILE:inventory-agentd>
)
set_tests_properties(inventory_integration_tests PROPERTIES
    TIMEOUT 90
    LABELS "integration"
)

add_executable(network_observation_integration_tests
    NetworkObservationIntegrationTests.cpp
)
target_compile_features(network_observation_integration_tests PRIVATE cxx_std_20)
target_link_libraries(network_observation_integration_tests
    PRIVATE
        rmc_fabric_integration_support
        network_observation_client
        network_observation_service
        network_observation_dbus_transport
        DBusCxx::DBusCxx
        jsoncpp
)
add_test(NAME network_observation_integration_tests COMMAND network_observation_integration_tests)
set_tests_properties(network_observation_integration_tests PROPERTIES
    TIMEOUT 90
    LABELS "integration"
)
