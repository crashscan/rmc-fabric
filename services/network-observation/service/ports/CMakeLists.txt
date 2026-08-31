add_library(network_observation_service_ports INTERFACE)
target_compile_features(network_observation_service_ports INTERFACE cxx_std_20)

target_include_directories(network_observation_service_ports
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(network_observation_service_ports INTERFACE interop_contract observation-model service_framework)
