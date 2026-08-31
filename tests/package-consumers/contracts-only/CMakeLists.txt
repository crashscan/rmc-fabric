cmake_minimum_required(VERSION 3.25)
project(rmc_fabric_contracts_consumer LANGUAGES CXX)

find_package(rmc_fabric CONFIG REQUIRED)

foreach(target_name
    rmc_fabric::inventory_service
    rmc_fabric::inventory_transport
    rmc_fabric::network_observation_service
    rmc_fabric::network_observation_dbus_transport
)
    if(TARGET ${target_name})
        message(FATAL_ERROR "unexpected internal exported target: ${target_name}")
    endif()
endforeach()

foreach(target_name
    rmc_fabric::interop_contract
    rmc_fabric::inventory-client
    rmc_fabric::inventory_dbus_codec
    rmc_fabric::network_observation_client
    rmc_fabric::network_observation_dbus_codec
)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "missing expected public target: ${target_name}")
    endif()
endforeach()

add_executable(contracts_only_consumer main.cpp)
target_compile_features(contracts_only_consumer PRIVATE cxx_std_20)
target_link_libraries(contracts_only_consumer PRIVATE rmc_fabric::interop_contract)
