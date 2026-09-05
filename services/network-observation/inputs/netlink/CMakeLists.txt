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

if(RSC_UTIL_INCLUDE_DIR)
    target_include_directories(netlink-monitor
            PRIVATE
            ${RSC_UTIL_INCLUDE_DIR}
            ${RSC_UTIL_INCLUDE_DIR}/rsc_util
    )
endif()

target_link_libraries(netlink-monitor
        PRIVATE
        pthread
        glog
        lifecycle_runner
        rsc_util
        netlink-event-loop
        netlink-initial-dump
)