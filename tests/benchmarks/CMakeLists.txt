add_executable(rmc_fabric_benchmarks
    RmcFabricBenchmarks.cpp
)
target_compile_features(rmc_fabric_benchmarks PRIVATE cxx_std_20)
target_link_libraries(rmc_fabric_benchmarks
    PRIVATE
        inventory_core
        inventory_dbus_codec
        network_observation_dbus_codec
        network_observation_service
        DBusCxx::DBusCxx
)
