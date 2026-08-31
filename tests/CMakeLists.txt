add_library(rmc_fabric_test_support STATIC
    support/DbusFixtureJson.cpp
)
target_compile_features(rmc_fabric_test_support PUBLIC cxx_std_20)
target_include_directories(rmc_fabric_test_support
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>
)
target_link_libraries(rmc_fabric_test_support
    PUBLIC
        interop_contract
    PRIVATE
        DBusCxx::DBusCxx
        jsoncpp
)

add_executable(contract_compatibility_tests
    ContractCompatibilityTests.cpp
)
target_compile_features(contract_compatibility_tests PRIVATE cxx_std_20)
target_compile_definitions(contract_compatibility_tests PRIVATE
    RMC_FABRIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(contract_compatibility_tests
    PRIVATE
        rmc_fabric_test_support
        inventory_dbus_codec
        network_observation_dbus_codec
        DBusCxx::DBusCxx
        jsoncpp
)
add_test(NAME contract_compatibility_tests COMMAND contract_compatibility_tests)
set_tests_properties(contract_compatibility_tests PROPERTIES
    TIMEOUT 20
    LABELS "unit"
)

if(RMC_FABRIC_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()

if(RMC_FABRIC_BUILD_FUZZ_TARGETS)
    add_subdirectory(fuzz)
endif()

if(RMC_FABRIC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
