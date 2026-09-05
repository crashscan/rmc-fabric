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
        rsc_util
)

add_library(netlink-monitor STATIC
        NetlinkNetworkMonitor.cpp
        parser/NetlinkParser.cpp
        state/NetlinkState.cpp
        utils/NetlinkUtils.cpp
)
target_compile_features(netlink-monitor PUBLIC cxx_std_20)
target_include_directories(netlink-monitor PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
)
target_include_directories(netlink-monitor PRIVATE
        public
        utils
        parser
        state
        dump
)
if(RSC_UTIL_INCLUDE_DIR)
    target_include_directories(netlink-monitor PRIVATE
            ${RSC_UTIL_INCLUDE_DIR}
            ${RSC_UTIL_INCLUDE_DIR}/rsc_util
    )
endif()

target_link_libraries(netlink-monitor
    PRIVATE
        pthread
        glog
        rsc_util
        netlink-initial-dump
)