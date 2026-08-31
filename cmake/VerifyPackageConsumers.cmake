set(CONSUMER_NAMES
    contracts-only
    inventory-client
    network-observation-client
)

function(configure_and_build_consumer source_dir build_dir prefix_path)
    file(REMOVE_RECURSE "${build_dir}")

    set(configure_args
        -S "${source_dir}"
        -B "${build_dir}"
        -G "${VERIFY_CONSUMER_GENERATOR}"
        -DCMAKE_BUILD_TYPE=${VERIFY_CONSUMER_BUILD_TYPE}
        -DCMAKE_PREFIX_PATH=${prefix_path}
        -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    )
    if(DEFINED VERIFY_CONSUMER_MAKE_PROGRAM AND NOT VERIFY_CONSUMER_MAKE_PROGRAM STREQUAL "")
        list(APPEND configure_args -DCMAKE_MAKE_PROGRAM=${VERIFY_CONSUMER_MAKE_PROGRAM})
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${configure_args}
        RESULT_VARIABLE configure_result
    )
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure consumer at ${source_dir}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}"
        RESULT_VARIABLE build_result
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build consumer at ${source_dir}")
    endif()
endfunction()

set(install_prefix "${VERIFY_CONSUMER_BINARY_DIR}/package-consumer-prefix")
file(REMOVE_RECURSE "${install_prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${VERIFY_CONSUMER_BINARY_DIR}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Failed to install rmc_fabric into ${install_prefix}")
endif()

foreach(consumer IN LISTS CONSUMER_NAMES)
    configure_and_build_consumer(
        "${VERIFY_CONSUMER_SOURCE_DIR}/tests/package-consumers/${consumer}"
        "${VERIFY_CONSUMER_BINARY_DIR}/consumer-build-tree/${consumer}"
        "${VERIFY_CONSUMER_BINARY_DIR}"
    )
    configure_and_build_consumer(
        "${VERIFY_CONSUMER_SOURCE_DIR}/tests/package-consumers/${consumer}"
        "${VERIFY_CONSUMER_BINARY_DIR}/consumer-install-tree/${consumer}"
        "${install_prefix}"
    )
endforeach()

set(required_paths
    "${install_prefix}/sbin/inventory-agentd"
    "${install_prefix}/sbin/network-observationd"
    "${install_prefix}/bin/net-observe"
    "${install_prefix}/etc/inventory-agent/inventory-agentd.conf"
    "${install_prefix}/etc/network-observation/network-observationd.conf"
    "${install_prefix}/etc/monit.d/inventory-agentd.cfg"
    "${install_prefix}/etc/monit.d/network-observationd.cfg"
    "${install_prefix}/etc/dbus-1/system.d/org.rsc.Inventory.conf"
    "${install_prefix}/etc/dbus-1/system.d/org.rsc.NetworkObservation.conf"
    "${install_prefix}/libexec/inventory-agent/start-inventory-agentd"
    "${install_prefix}/libexec/network-observation/start-network-observationd"
)

foreach(required_path IN LISTS required_paths)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing installed runtime artifact: ${required_path}")
    endif()
endforeach()

function(run_help_smoke executable)
    execute_process(
        COMMAND "${executable}" --help
        RESULT_VARIABLE help_result
        OUTPUT_VARIABLE help_stdout
        ERROR_VARIABLE help_stderr
    )
    if(NOT help_result EQUAL 0 AND NOT help_result EQUAL 1)
        message(FATAL_ERROR "Help smoke test failed for ${executable}")
    endif()
    string(LENGTH "${help_stdout}${help_stderr}" help_length)
    if(help_length EQUAL 0)
        message(FATAL_ERROR "Help smoke test produced no output for ${executable}")
    endif()
endfunction()

run_help_smoke("${install_prefix}/sbin/inventory-agentd")
run_help_smoke("${install_prefix}/sbin/network-observationd")
run_help_smoke("${install_prefix}/bin/net-observe")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DRELEASE_MANIFEST_SOURCE_DIR=${VERIFY_CONSUMER_SOURCE_DIR}
        -DRELEASE_MANIFEST_INSTALL_PREFIX=${install_prefix}
        -DRELEASE_MANIFEST_OUTPUT=${VERIFY_CONSUMER_BINARY_DIR}/release-manifest.json
        -P "${VERIFY_CONSUMER_SOURCE_DIR}/cmake/GenerateReleaseManifest.cmake"
    RESULT_VARIABLE manifest_result
)
if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "Failed to generate release manifest")
endif()
