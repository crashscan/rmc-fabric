if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "RMC_FABRIC_BUILD_FUZZ_TARGETS requires Clang/libFuzzer")
endif()

add_executable(inventory_dbus_codec_fuzz
    InventoryDbusCodecFuzz.cpp
)
target_compile_features(inventory_dbus_codec_fuzz PRIVATE cxx_std_20)
target_compile_options(inventory_dbus_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(inventory_dbus_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
target_link_libraries(inventory_dbus_codec_fuzz
    PRIVATE
        rmc_fabric_test_support
        inventory_dbus_codec
        DBusCxx::DBusCxx
        jsoncpp
)

add_executable(network_observation_dbus_codec_fuzz
    NetworkObservationDbusCodecFuzz.cpp
)
target_compile_features(network_observation_dbus_codec_fuzz PRIVATE cxx_std_20)
target_compile_options(network_observation_dbus_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(network_observation_dbus_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
target_link_libraries(network_observation_dbus_codec_fuzz
    PRIVATE
        rmc_fabric_test_support
        network_observation_dbus_codec
        DBusCxx::DBusCxx
        jsoncpp
)

add_test(
    NAME inventory_dbus_codec_fuzz_smoke
    COMMAND inventory_dbus_codec_fuzz -runs=200 ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus/inventory
)
set_tests_properties(inventory_dbus_codec_fuzz_smoke PROPERTIES
    TIMEOUT 60
    LABELS "fuzz-smoke"
)

add_test(
    NAME network_observation_dbus_codec_fuzz_smoke
    COMMAND network_observation_dbus_codec_fuzz -runs=200 ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus/network-observation
)
set_tests_properties(network_observation_dbus_codec_fuzz_smoke PROPERTIES
    TIMEOUT 60
    LABELS "fuzz-smoke"
)
