add_library(netlink-io STATIC
        common/NetlinkIo.cpp
)

target_compile_features(netlink-io PUBLIC cxx_std_20)

target_include_directories(netlink-io
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
)

target_link_libraries(netlink-io
        PUBLIC
        rsc_util
        PRIVATE
        glog
)

add_library(netlink-initial-dump STATIC
        dump/NetlinkInitialDump.cpp
)

target_compile_features(netlink-initial-dump PUBLIC cxx_std_20)

target_include_directories(netlink-initial-dump
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dump>
)

target_link_libraries(netlink-initial-dump
        PUBLIC
        netlink-io
)

add_library(netlink-event-loop STATIC
        live/NetlinkEventLoop.cpp
)

target_compile_features(netlink-event-loop PUBLIC cxx_std_20)

target_include_directories(netlink-event-loop
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/live>
)

target_link_libraries(netlink-event-loop
        PUBLIC
        netlink-io
)

add_library(netlink-monitor STATIC
        NetlinkNetworkMonitor.cpp
        parser/NetlinkParser.cpp
        state/NetlinkState.cpp
        utils/NetlinkUtils.cpp
)

target_compile_features(netlink-monitor PUBLIC cxx_std_20)

target_include_directories(netlink-monitor
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        PRIVATE
        public
        utils
        parser
        state
)

target_link_libraries(netlink-monitor
        PRIVATE
        pthread
        glog
        rsc_util::service_runtime
        rsc_util
        netlink-event-loop
        netlink-initial-dump
)

if (BUILD_TESTING)
    add_executable(netlink_io_tests
            tests/NetlinkIoTests.cpp
    )

    target_compile_features(
            netlink_io_tests
            PRIVATE
            cxx_std_20
    )

    target_link_libraries(
            netlink_io_tests
            PRIVATE
            netlink-io
            pthread
    )

    add_test(
            NAME netlink_io_tests
            COMMAND netlink_io_tests
    )

    set_tests_properties(
            netlink_io_tests
            PROPERTIES
            TIMEOUT 10
            LABELS "unit;netlink"
    )

    add_executable(netlink_monitor_tests
            tests/NetlinkNetworkMonitorTests.cpp
            tests/support/NetlinkNetworkMonitorFactory.cpp
    )

    target_include_directories(
            netlink_monitor_tests
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
    )

    target_compile_features(
            netlink_monitor_tests
            PRIVATE
            cxx_std_20
    )

    target_link_libraries(
            netlink_monitor_tests
            PRIVATE
            netlink-monitor
            pthread
    )

    add_test(
            NAME netlink_monitor_tests
            COMMAND netlink_monitor_tests
    )

    set_tests_properties(
            netlink_monitor_tests
            PROPERTIES
            TIMEOUT 15
            LABELS "unit;netlink;resilience"
    )

endif ()