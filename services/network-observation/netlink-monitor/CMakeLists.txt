find_package(DBusCxx REQUIRED)

# ---------------------------------------------------------------------------
# libnetlink-monitor.so — raw netlink event monitor
# ---------------------------------------------------------------------------
add_library(netlink-monitor SHARED
        NetlinkNetworkMonitor.cpp
        parser/NetlinkParser.cpp
        state/NetlinkState.cpp
        utils/NetlinkUtils.cpp
)
target_include_directories(netlink-monitor PRIVATE
        ${RSC_UTIL_INCLUDE_DIR}
        ${RSC_UTIL_INCLUDE_DIR}/rsc_util
        types
        utils
        parser
        state
)
target_link_libraries(netlink-monitor pthread glog rsc_util)