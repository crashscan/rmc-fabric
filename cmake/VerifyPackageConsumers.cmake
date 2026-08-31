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
